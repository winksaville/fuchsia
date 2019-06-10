// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! IPv6 specific functionality.

use std::iter::Iterator;

use zerocopy::ByteSlice;

use crate::device::{DeviceId, FrameDestination};
use crate::wire::ipv6::ext_hdrs::{
    DestinationOptionData, ExtensionHeaderOption, FragmentData, HopByHopOptionData,
    Ipv6ExtensionHeaderData, RoutingData,
};
use crate::wire::ipv6::Ipv6Packet;
use crate::{Context, EventDispatcher};

/// What to do with an IPv6 packet after parsing an extension header.
#[derive(Debug, PartialEq, Eq)]
pub(crate) enum Ipv6PacketAction {
    /// Discard the packet.
    Discard,

    /// Continue processing the next extension header (if any are
    /// available and the processing node is the destination node)
    /// or continue processing the packet (if the extension headers
    /// have been exhausted or the processing node is not the
    /// destination node).
    Continue,

    /// Stop processing extension headers and consider the
    /// packet fragmented. The node must attempt to handle
    /// the fragmented packet (attempt reassembly).
    ProcessFragment,
}

/// Handle IPv6 extension headers.
///
/// What this function does depents on whether or not the `at_destination` flag
/// is set. If it is `true`, then we will attempt to process all the extension
/// headers in `packet`. Otherwise, we will only attempt to process the hop-by-hop
/// extension header (which MUST be the first extension header if present) as
/// per RFC 8200 section 4.
pub(crate) fn handle_extension_headers<D: EventDispatcher, B: ByteSlice>(
    ctx: &mut Context<D>,
    device: DeviceId,
    frame_dst: FrameDestination,
    packet: &Ipv6Packet<B>,
    at_destination: bool,
) -> Ipv6PacketAction {
    // The next action we need to do after processing an extension header.
    //
    // Initialized to `Ipv6PacketAction::Continue` so we start off processing
    // extension headers.
    let mut action = Ipv6PacketAction::Continue;
    let mut iter = packet.iter_extension_hdrs();

    if at_destination {
        // Keep looping while we are okay to just continue parsing extension headers.
        while action == Ipv6PacketAction::Continue {
            let ext_hdr = match iter.next() {
                None => break,
                Some(x) => x,
            };

            match ext_hdr.data() {
                Ipv6ExtensionHeaderData::HopByHopOptions { options } => {
                    action = handle_hop_by_hop_options_ext_hdr(
                        ctx,
                        device,
                        frame_dst,
                        packet,
                        options.iter(),
                    );
                }
                Ipv6ExtensionHeaderData::Routing { routing_data } => {
                    action = handle_routing_ext_hdr(ctx, device, frame_dst, packet, routing_data);
                }
                Ipv6ExtensionHeaderData::Fragment { fragment_data } => {
                    action = handle_fragment_ext_hdr(ctx, device, frame_dst, packet, fragment_data);
                }
                Ipv6ExtensionHeaderData::DestinationOptions { options } => {
                    action = handle_destination_options_ext_hdr(
                        ctx,
                        device,
                        frame_dst,
                        packet,
                        options.iter(),
                    );
                }
            }
        }
    } else {
        // Packet is not yet at the destination, so only process the hop-by-hop
        // options extension header (which MUST be the first extension header if
        // it is present) as per RFC 8200 section 4.
        if let Some(ext_hdr) = iter.next() {
            if let Ipv6ExtensionHeaderData::HopByHopOptions { options } = ext_hdr.data() {
                action = handle_hop_by_hop_options_ext_hdr(
                    ctx,
                    device,
                    frame_dst,
                    packet,
                    options.iter(),
                );
            }
        }
    }

    action
}

/// Handles a Hop By Hop extension header for a `packet`.
// For now, we do not support any options. If parsing succeeds we are
// guaranteed that the options present are safely skippable. If they
// aren't safely skippable, we must have resulted in a parsing error
// when parsing the packet, and so this function will never be called.
fn handle_hop_by_hop_options_ext_hdr<
    'a,
    D: EventDispatcher,
    B: ByteSlice,
    I: Iterator<Item = ExtensionHeaderOption<HopByHopOptionData<'a>>>,
>(
    ctx: &mut Context<D>,
    device: DeviceId,
    frame_dst: FrameDestination,
    packet: &Ipv6Packet<B>,
    options: I,
) -> Ipv6PacketAction {
    for option in options {
        match option.data {
            // Safely skip and continue, as we know that if we parsed an unrecognized
            // option, the option's action was set to skip and continue.
            HopByHopOptionData::Unrecognized { kind, len, data } => {}
        }
    }

    Ipv6PacketAction::Continue
}

/// Handles a routing extension header for a `packet`.
fn handle_routing_ext_hdr<'a, D: EventDispatcher, B: ByteSlice>(
    ctx: &mut Context<D>,
    device: DeviceId,
    frame_dst: FrameDestination,
    packet: &Ipv6Packet<B>,
    routing_data: &RoutingData<'a>,
) -> Ipv6PacketAction {
    // We should never end up here because we do not support parsing any routing header
    // type yet. We should have errored out while parsing the extension header if
    // there is a routing header we would normally have to act on.
    unreachable!("We should not end up here because no routing type is supported yet");
}

/// Handles a fragment extension header for a `packet`.
fn handle_fragment_ext_hdr<'a, D: EventDispatcher, B: ByteSlice>(
    ctx: &mut Context<D>,
    device: DeviceId,
    frame_dst: FrameDestination,
    packet: &Ipv6Packet<B>,
    fragment_data: &FragmentData<'a>,
) -> Ipv6PacketAction {
    panic!("Handling fragments is not implemented yet");
}

/// Handles a destination extension header for a `packet`.
// For now, we do not support any options. If parsing succeeds we are
// guaranteed that the options present are safely skippable. If they
// aren't safely skippable, we must have resulted in a parsing error
// when parsing the packet, and so this function will never be called.
fn handle_destination_options_ext_hdr<
    'a,
    D: EventDispatcher,
    B: ByteSlice,
    I: Iterator<Item = ExtensionHeaderOption<DestinationOptionData<'a>>>,
>(
    ctx: &mut Context<D>,
    device: DeviceId,
    frame_dst: FrameDestination,
    packet: &Ipv6Packet<B>,
    options: I,
) -> Ipv6PacketAction {
    for option in options {
        match option.data {
            // Safely skip and continue, as we know that if we parsed an unrecognized
            // option, the option's action was set to skip and continue.
            DestinationOptionData::Unrecognized { kind, len, data } => {}
        }
    }

    Ipv6PacketAction::Continue
}

#[cfg(test)]
mod tests {
    use super::*;

    use crate::ip::IpProto;
    use crate::testutil::{DummyEventDispatcher, DummyEventDispatcherBuilder, DUMMY_CONFIG_V6};
    use crate::wire::ipv6::{Ipv6Packet, Ipv6PacketBuilder};
    use packet::serialize::{Buf, BufferSerializer, Serializer};
    use packet::ParseBuffer;

    #[test]
    fn test_no_extension_headers() {
        //
        // Test that if we have no extension headers, we continue
        //

        let mut ctx = DummyEventDispatcherBuilder::from_config(DUMMY_CONFIG_V6)
            .build::<DummyEventDispatcher>();
        let builder = Ipv6PacketBuilder::new(
            DUMMY_CONFIG_V6.remote_ip,
            DUMMY_CONFIG_V6.local_ip,
            10,
            IpProto::Tcp,
        );
        let device_id = DeviceId::new_ethernet(1);
        let frame_dst = FrameDestination::Unicast;
        let mut buffer = BufferSerializer::new_vec(Buf::new(vec![1, 2, 3, 4, 5], ..))
            .encapsulate(builder)
            .serialize_outer()
            .unwrap();
        let mut packet = buffer.parse::<Ipv6Packet<_>>().unwrap();

        assert_eq!(
            handle_extension_headers(&mut ctx, device_id, frame_dst, &packet, false),
            Ipv6PacketAction::Continue
        );
    }
}
