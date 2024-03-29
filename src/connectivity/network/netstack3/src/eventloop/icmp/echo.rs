// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implementation of fuchsia.net.icmp.EchoSocket.

use failure::Fail;
use futures::channel::mpsc;
use futures::{select, StreamExt};
use log::{error, trace};
use std::collections::VecDeque;
use std::fmt;
use std::sync::{Arc, Mutex};

use fuchsia_async as fasync;
use fuchsia_zircon as zx;

use fidl_fuchsia_net_icmp::{
    EchoPacket, EchoSocketRequest, EchoSocketRequestStream, EchoSocketWatchResponder,
    EchoSocketWatchResult,
};

use net_types::ip::IpVersion;
use netstack3_core::icmp as core_icmp;
use netstack3_core::icmp::IcmpConnId;
use netstack3_core::{BufferDispatcher, Context};
use packet_new::{Buf, BufferMut};

use crate::eventloop::icmp::RX_BUFFER_SIZE;
use crate::eventloop::{Event, EventLoop};

/// A request event for an [`EchoSocketWorker`].
#[derive(Debug)]
pub struct Request {
    worker: Arc<Mutex<EchoSocketWorkerInner<EchoSocketWatchResponder, IcmpConnId>>>,
    request: EchoSocketRequest,
}

impl Request {
    /// Handles this [`EchoSocketRequest`] with the provided `EventLoop` context.
    pub fn handle_request(self, event_loop: &mut EventLoop) {
        self.worker.lock().unwrap().handle_request(event_loop, self.request);
    }
}

/// Worker for handling requests from a [`fidl_fuchsia_net_icmp::EchoSocketRequestStream`].
pub struct EchoSocketWorker {
    stream: EchoSocketRequestStream,
    reply_rx: mpsc::Receiver<EchoPacket>,
    inner: Arc<Mutex<EchoSocketWorkerInner<EchoSocketWatchResponder, IcmpConnId>>>,
}

impl EchoSocketWorker {
    /// Create a new EchoSocketWorker, a wrapper around a background worker that handles requests
    /// from a [`fidl_fuchsia_net_icmp::EchoSocketRequestStream`].
    pub fn new(
        stream: EchoSocketRequestStream,
        reply_rx: mpsc::Receiver<EchoPacket>,
        net_proto: IpVersion,
        conn: IcmpConnId,
        icmp_id: u16,
    ) -> EchoSocketWorker {
        EchoSocketWorker {
            stream,
            reply_rx,
            inner: Arc::new(Mutex::new(EchoSocketWorkerInner::new(
                net_proto,
                conn,
                icmp_id,
                RX_BUFFER_SIZE,
            ))),
        }
    }

    /// Spawn a background worker to handle requests from a
    /// [`fidl_fuchsia_net_icmp::EchoSocketRequestStream`].
    pub fn spawn(mut self, sender: mpsc::UnboundedSender<Event>) {
        fasync::spawn_local(async move {
            loop {
                select! {
                    evt = self.stream.next() => {
                        if let Some(Ok(e)) = evt {
                            let evt = Event::FidlEchoSocketEvent(Request {
                                worker: Arc::clone(&self.inner),
                                request: e,
                            });
                            if let Err(e) = sender.unbounded_send(evt) {
                                error!("Failed to send echo socket event: {:?}", e);
                            }
                        } else {
                            // Client has closed the request stream.
                            return;
                        }
                    },
                    reply = self.reply_rx.next() => {
                        if let Some(r) = reply {
                            let mut worker = match self.inner.lock() {
                                Ok(w) => w,
                                Err(e) => {
                                    error!("Mutex has been poisoned: {:?}", e);
                                    return;
                                }
                            };
                            if let Err(e) = worker.handle_reply(r) {
                                error!("Failed to handle reply: {:?}", e);
                            }
                        } else {
                            // Netstack3 has closed the EchoPacket channel.
                            return;
                        }
                    },
                    complete => return,
                };
            }
        });
    }
}

/// Error type for working with ICMP echo sockets.
#[derive(Fail, Debug)]
pub enum ResponderError {
    #[fail(display = "FIDL failure: {}", _0)]
    Fidl(fidl::Error),
    #[fail(display = "Reached reply buffer capacity")]
    ReachedCapacity,
}

/// `Responder` represents a request to watch for a result. The request is completed once
/// `respond` is called with an `EchoSocketWatchResult`, consuming the `Responder`. This
/// abstraction allows for mocking.
pub trait Responder {
    /// Send a response.
    fn respond(self, result: &mut EchoSocketWatchResult) -> Result<(), ResponderError>;
}

impl Responder for EchoSocketWatchResponder {
    fn respond(self, result: &mut EchoSocketWatchResult) -> Result<(), ResponderError> {
        self.send(result).map_err(|e| ResponderError::Fidl(e))
    }
}

/// Implementation for an `EchoSocket`.
#[derive(Debug)]
pub struct EchoSocketWorkerInner<R: Responder, C> {
    net_proto: IpVersion,
    conn: C,
    icmp_id: u16,
    results: VecDeque<EchoSocketWatchResult>,
    responders: VecDeque<R>,
    capacity: usize,
}

impl<R: Responder, C: fmt::Debug> EchoSocketWorkerInner<R, C> {
    fn new(net_proto: IpVersion, conn: C, icmp_id: u16, capacity: usize) -> Self {
        EchoSocketWorkerInner {
            net_proto,
            conn,
            icmp_id,
            capacity,
            results: VecDeque::with_capacity(capacity + 1), // one additional element for IO_OVERRUN
            responders: VecDeque::with_capacity(capacity + 1),
        }
    }

    fn handle_reply(&mut self, reply: EchoPacket) -> Result<(), ResponderError> {
        trace!("Handling an ICMP Echo reply: {:?}", reply);
        if self.results.len() > self.capacity {
            return Err(ResponderError::ReachedCapacity);
        }
        if self.results.len() == self.capacity {
            self.results.push_back(Err(zx::Status::into_raw(zx::Status::IO_OVERRUN)));
            return self.respond().and(Err(ResponderError::ReachedCapacity));
        }
        self.results.push_back(Ok(reply));
        self.respond()
    }

    fn respond(&mut self) -> Result<(), ResponderError> {
        if !self.results.is_empty() && !self.responders.is_empty() {
            let mut result = self.results.pop_front().unwrap(); // Just checked if empty
            let responder = self.responders.pop_front().unwrap();
            responder.respond(&mut result)?;
        }
        Ok(())
    }

    fn watch(&mut self, responder: R) -> Result<(), ResponderError> {
        trace!("Watching for an ICMP Echo reply for {:?} on {:?}", self.conn, self.net_proto);
        self.responders.push_back(responder);
        self.respond()
    }
}

impl EchoSocketWorkerInner<EchoSocketWatchResponder, IcmpConnId> {
    /// Handle a `fidl_fuchsia_net_icmp::EchoSocketRequest`, which is used for sending ICMP echo
    /// requests and receiving ICMP echo replies.
    fn handle_request(&mut self, event_loop: &mut EventLoop, req: EchoSocketRequest) {
        match req {
            EchoSocketRequest::SendRequest { mut request, .. } => {
                let len = request.payload.len();
                self.send_request(
                    &mut event_loop.ctx,
                    request.sequence_num,
                    Buf::new(&mut request.payload.as_mut_slice()[..len], ..),
                );
            }
            EchoSocketRequest::Watch { responder } => {
                match self.watch(responder) {
                    Ok(_) => {}
                    Err(e) => {
                        error!("Failed to watch an ICMP echo socket: {:?}", e);
                    }
                };
            }
        }
    }

    fn send_request<B: BufferMut, D: BufferDispatcher<B>>(
        &self,
        ctx: &mut Context<D>,
        seq_num: u16,
        payload: B,
    ) {
        trace!("Sending ICMP Echo request for {:?} w/ sequence number {}", self.conn, seq_num);
        match self.net_proto {
            IpVersion::V4 => core_icmp::send_icmpv4_echo_request(ctx, self.conn, seq_num, payload),
            IpVersion::V6 => core_icmp::send_icmpv6_echo_request(ctx, self.conn, seq_num, payload),
        };
        // TODO(fxb/37143): Report ICMP errors to responders, once implemented in the core, by
        // pushing a zx::Status to `self.results`.
    }
}

#[cfg(test)]
mod test {
    use fidl_fuchsia_net_icmp::{EchoPacket, EchoSocketWatchResult};
    use fuchsia_zircon as zx;
    use net_types::ip::IpVersion;

    use super::{EchoSocketWorkerInner, Responder, ResponderError};

    struct TestResponder {
        expected: Result<EchoPacket, zx::Status>,
    }

    impl Responder for TestResponder {
        fn respond(self, result: &mut EchoSocketWatchResult) -> Result<(), ResponderError> {
            assert_eq!(self.expected, result.clone().map_err(|e| zx::Status::from_raw(e)));
            Ok(())
        }
    }

    fn create_worker_with_capacity(capacity: usize) -> EchoSocketWorkerInner<TestResponder, u8> {
        EchoSocketWorkerInner::new(IpVersion::V4, 1, 1, capacity)
    }

    #[test]
    fn test_icmp_echo_socket_worker_no_responder() {
        let mut worker = create_worker_with_capacity(1);

        let payload = vec![1, 2, 3, 4, 5];
        let packet = EchoPacket { sequence_num: 1, payload };

        assert!(worker.handle_reply(packet).is_ok());
    }

    #[test]
    fn test_icmp_echo_socket_worker_no_reply() {
        let mut worker = create_worker_with_capacity(1);

        let payload = vec![1, 2, 3, 4, 5];
        let packet = EchoPacket { sequence_num: 1, payload };

        assert!(worker.watch(TestResponder { expected: Ok(packet) }).is_ok());
    }

    #[test]
    fn test_icmp_echo_socket_worker_reply_then_watch() {
        let mut worker = create_worker_with_capacity(1);

        let payload = vec![1, 2, 3, 4, 5];
        let packet = EchoPacket { sequence_num: 1, payload };

        worker.handle_reply(packet.clone()).unwrap();
        worker.watch(TestResponder { expected: Ok(packet) }).unwrap();
    }

    #[test]
    fn test_icmp_echo_socket_worker_watch_then_reply() {
        let mut worker = create_worker_with_capacity(1);

        let payload = vec![1, 2, 3, 4, 5];
        let packet = EchoPacket { sequence_num: 1, payload };

        worker.watch(TestResponder { expected: Ok(packet.clone()) }).unwrap();
        worker.handle_reply(packet).unwrap();
    }

    #[test]
    fn test_icmp_echo_socket_worker_reply_in_order() {
        let mut worker = create_worker_with_capacity(2);

        let first_packet = EchoPacket { sequence_num: 1, payload: vec![0, 1, 2, 3, 4] };
        let second_packet = EchoPacket { sequence_num: 2, payload: vec![5, 6, 7, 8, 9] };

        worker.handle_reply(first_packet.clone()).unwrap();
        worker.handle_reply(second_packet.clone()).unwrap();
        worker.watch(TestResponder { expected: Ok(first_packet) }).unwrap();
        worker.watch(TestResponder { expected: Ok(second_packet) }).unwrap();
    }

    #[test]
    fn test_icmp_echo_socket_worker_watch_in_order() {
        let mut worker = create_worker_with_capacity(2);

        let first_packet = EchoPacket { sequence_num: 1, payload: vec![0, 1, 2, 3, 4] };
        let second_packet = EchoPacket { sequence_num: 2, payload: vec![5, 6, 7, 8, 9] };

        worker.watch(TestResponder { expected: Ok(first_packet.clone()) }).unwrap();
        worker.watch(TestResponder { expected: Ok(second_packet.clone()) }).unwrap();
        worker.handle_reply(first_packet).unwrap();
        worker.handle_reply(second_packet).unwrap();
    }

    #[test]
    fn test_icmp_echo_socket_worker_reply_over_capacity() {
        let mut worker = create_worker_with_capacity(1);

        let first_packet = EchoPacket { sequence_num: 1, payload: vec![0, 1, 2, 3, 4] };
        let second_packet = EchoPacket { sequence_num: 2, payload: vec![5, 6, 7, 8, 9] };

        worker.handle_reply(first_packet.clone()).unwrap();
        worker.handle_reply(second_packet.clone()).unwrap_err();
        worker.watch(TestResponder { expected: Ok(first_packet) }).unwrap();
        worker.watch(TestResponder { expected: Err(zx::Status::IO_OVERRUN) }).unwrap();
    }

    #[test]
    fn test_icmp_echo_socket_worker_reply_over_capacity_recover() {
        let mut worker = create_worker_with_capacity(1);

        let first_packet = EchoPacket { sequence_num: 1, payload: vec![0, 1, 2, 3, 4] };
        let second_packet = EchoPacket { sequence_num: 2, payload: vec![5, 6, 7, 8, 9] };
        let third_packet = EchoPacket { sequence_num: 3, payload: vec![2, 4, 6, 8, 0] };

        worker.handle_reply(first_packet.clone()).unwrap();
        worker.handle_reply(second_packet).unwrap_err(); // second_packet should be dropped
        worker.watch(TestResponder { expected: Ok(first_packet) }).unwrap();
        worker.watch(TestResponder { expected: Err(zx::Status::IO_OVERRUN) }).unwrap();

        worker.handle_reply(third_packet.clone()).unwrap();
        worker.watch(TestResponder { expected: Ok(third_packet) }).unwrap();
    }

    #[test]
    fn test_icmp_echo_socket_worker_reply_over_capacity_twice_recover() {
        let mut worker = create_worker_with_capacity(1);

        let first_packet = EchoPacket { sequence_num: 1, payload: vec![0, 1, 2, 3, 4] };
        let second_packet = EchoPacket { sequence_num: 2, payload: vec![5, 6, 7, 8, 9] };
        let third_packet = EchoPacket { sequence_num: 3, payload: vec![2, 4, 6, 8, 0] };
        let fourth_packet = EchoPacket { sequence_num: 4, payload: vec![1, 3, 5, 7, 9] };

        worker.handle_reply(first_packet.clone()).unwrap();
        worker.handle_reply(second_packet).unwrap_err(); // second_packet should be dropped
        worker.handle_reply(third_packet).unwrap_err(); // third_packet should be dropped
        worker.watch(TestResponder { expected: Ok(first_packet) }).unwrap();
        worker.watch(TestResponder { expected: Err(zx::Status::IO_OVERRUN) }).unwrap();

        worker.handle_reply(fourth_packet.clone()).unwrap();
        worker.watch(TestResponder { expected: Ok(fourth_packet) }).unwrap();
    }

    #[test]
    fn test_icmp_echo_socket_worker_watch_over_capacity_recover() {
        let mut worker = create_worker_with_capacity(1);

        let first_packet = EchoPacket { sequence_num: 1, payload: vec![0, 1, 2, 3, 4] };
        let second_packet = EchoPacket { sequence_num: 2, payload: vec![5, 6, 7, 8, 9] };
        let third_packet = EchoPacket { sequence_num: 3, payload: vec![2, 4, 6, 8, 0] };
        let fourth_packet = EchoPacket { sequence_num: 4, payload: vec![1, 3, 5, 7, 9] };

        worker.watch(TestResponder { expected: Ok(first_packet.clone()) }).unwrap();
        worker.watch(TestResponder { expected: Ok(second_packet.clone()) }).unwrap();
        worker.handle_reply(first_packet).unwrap();
        worker.handle_reply(second_packet).unwrap();
        worker.handle_reply(third_packet.clone()).unwrap();
        worker.handle_reply(fourth_packet).unwrap_err(); // fourth_packet should be dropped
        worker.watch(TestResponder { expected: Ok(third_packet) }).unwrap();
        worker.watch(TestResponder { expected: Err(zx::Status::IO_OVERRUN) }).unwrap();
    }

    #[test]
    fn test_icmp_echo_socket_worker_watch_over_capacity_twice_recover() {
        let mut worker = create_worker_with_capacity(1);

        let first_packet = EchoPacket { sequence_num: 1, payload: vec![0, 1, 2, 3, 4] };
        let second_packet = EchoPacket { sequence_num: 2, payload: vec![5, 6, 7, 8, 9] };
        let third_packet = EchoPacket { sequence_num: 3, payload: vec![2, 4, 6, 8, 0] };
        let fourth_packet = EchoPacket { sequence_num: 4, payload: vec![1, 3, 5, 7, 9] };

        worker.watch(TestResponder { expected: Ok(first_packet.clone()) }).unwrap();
        worker.handle_reply(first_packet).unwrap();
        worker.handle_reply(second_packet.clone()).unwrap();
        worker.handle_reply(third_packet.clone()).unwrap_err(); // third_packet should be dropped
        worker.watch(TestResponder { expected: Ok(second_packet) }).unwrap();
        worker.watch(TestResponder { expected: Err(zx::Status::IO_OVERRUN) }).unwrap();
        worker.watch(TestResponder { expected: Ok(fourth_packet.clone()) }).unwrap();
        worker.handle_reply(fourth_packet.clone()).unwrap();
    }
}
