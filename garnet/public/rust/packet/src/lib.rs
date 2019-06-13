// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Parsing and serialization of (network) packets.
//!
//! `packet` is a library to help with the parsing and serialization of nested
//! packets. Network packets are the most common use case, but it supports any
//! packet structure with headers, footers, and nesting.
//!
//! # Model
//!
//! The core component of `packet` is the [`Buffer`] trait. A `Buffer` is a byte
//! buffer with a prefix, a body, and a suffix. The size of the buffer is
//! referred to as its "capacity", and the size of the body is referred to as
//! its "length". The body of the buffer can shrink or grow as allowed by the
//! capacity as packets are parsed or serialized.
//!
//! ## Parsing
//!
//! When parsing packets, the body of the buffer stores the next packet to be
//! parsed. When a packet is parsed from the buffer, any headers, footers, and
//! padding are "consumed" from the buffer. Thus, after a packet has been
//! parsed, the body of the buffer is equal to the body of the packet, and the
//! next call to `parse` will pick up where the previous call left off, parsing
//! the next encapsulated packet.
//!
//! Packet objects - the Rust objects which are the result of a successful
//! parsing - are advised to simply keep references into the buffer for the
//! header, footer, and body. This avoids any unnecessary copying. `Buffer`'s
//! parsing methods make this easy.
//!
//! For example, consider the following packet structure, in which a TCP segment
//! is encapsulated in an IPv4 packet, which is encapsulated in an Ethernet
//! frame. In this example, we omit the Ethernet Frame Check Sequence (FCS)
//! footer. If there were any footers, they would be treated the same as
//! headers, except that they would be consumed from the end and working towards
//! the beginning, as opposed to headers, which are consumed from the beginning
//! and working towards the end.
//!
//! Also note that, in order to satisfy Ethernet's minimum body size
//! requirement, padding is added after the IPv4 packet. The IPv4 packet and
//! padding together are considered the body of the Ethernet frame. If we were
//! to include the Ethernet FCS footer in this example, it would go after the
//! padding.
//!
//! ```text
//! |-------------------------------------|++++++++++++++++++++|-----| TCP segment
//! |-----------------|++++++++++++++++++++++++++++++++++++++++|-----| IPv4 packet
//! |++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++| Ethernet frame
//!
//! |-----------------|-------------------|--------------------|-----|
//!   Ethernet header      IPv4 header         TCP segment      Padding
//! ```
//!
//! At first, the buffer's body would be equal to the bytes of the Ethernet
//! frame (although depending on how the buffer was initialized, it might have
//! extra capacity in addition to the body):
//!
//! ```text
//! |-------------------------------------|++++++++++++++++++++|-----| TCP segment
//! |-----------------|++++++++++++++++++++++++++++++++++++++++|-----| IPv4 packet
//! |++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++| Ethernet frame
//!
//! |-----------------|-------------------|--------------------|-----|
//!   Ethernet header      IPv4 header         TCP segment      Padding
//!
//! |----------------------------------------------------------------|
//!                             Buffer Body
//! ```
//!
//! First, the Ethernet frame is parsed. This results in a hypothetical
//! `EthernetFrame` object (this library does not provide any concrete parsing
//! implementations) with references into the buffer, and updates the body of
//! the buffer to be equal to the body of the Ethernet frame:
//!
//! ```text
//! |-------------------------------------|++++++++++++++++++++|-----| TCP segment
//! |-----------------|++++++++++++++++++++++++++++++++++++++++|-----| IPv4 packet
//! |++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++| Ethernet frame
//!
//! |-----------------|----------------------------------------------|
//!   Ethernet header                  Ethernet body
//!          |                                 |
//!          +--------------------------+      |
//!                                     |      |
//!                   EthernetFrame { header, body }
//!
//! |-----------------|----------------------------------------------|
//!    Buffer prefix                   Buffer body
//! ```
//!
//! The `EthernetFrame` object mutably borrows the buffer. So long as it exists,
//! the buffer cannot be used directly (although the `EthernetFrame` object may
//! be used to access or modify the contents of the buffer). In order to parse
//! the body of the Ethernet frame, we have to drop the `EthernetFrame` object
//! so that we can call methods on the buffer again. \[1\]
//!
//! After dropping the `EthernetFrame` object, the IPv4 packet is parsed. Recall
//! that the Ethernet body contains both the IPv4 packet and some padding. Since
//! IPv4 packets encode their own length, the IPv4 packet parser is able to
//! detect that some of the bytes it's operating on are padding bytes. It is the
//! parser's responsibility to consume and discard these bytes so that they are
//! not erroneously treated as part of the IPv4 packet's body in subsequent
//! parsings.
//!
//! This parsing results in a hypothetical `Ipv4Packet` object with references
//! into the buffer, and updates the body of the buffer to be equal to the body
//! of the IPv4 packet:
//!
//! ```text
//! |-------------------------------------|++++++++++++++++++++|-----| TCP segment
//! |-----------------|++++++++++++++++++++++++++++++++++++++++|-----| IPv4 packet
//! |++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++| Ethernet frame
//!
//! |-----------------|-------------------|--------------------|-----|
//!                        IPv4 header          IPv4 body
//!                             |                   |
//!                             +-----------+       |
//!                                         |       |
//!                          Ipv4Packet { header, body }
//!
//! |-------------------------------------|--------------------|-----|
//!              Buffer prefix                 Buffer body       Buffer suffix
//! ```
//!
//! We can continue this process as long as we like, repeatedly parsing
//! subsequent packet bodies until there are no more packets to parse.
//!
//! \[1\] It is also possible to treat the `EthernetFrame`'s `body` field as a
//! `Buffer` and parse from it directly. However, this has the disadvantage that
//! if parsing is spread across multiple functions, the functions which parse
//! the inner packets only see part of the buffer, and so if they wish to later
//! re-use the buffer for serializing new packets (see the "Serialization"
//! section of this documentation), they are limited to doing so in a smaller
//! buffer, making it more likely that a new buffer will need to be allocated.
//!
//! ## Serialization
//!
//! In this section, we will illustrate serialization using the same packet
//! structure that was used to illustrate parsing - a TCP segment in an IPv4
//! packet in an Ethernet frame.
//!
//! Serialization comprises two tasks:
//! - First, given a buffer with sufficient capacity, and part of the packet
//!   already serialized, serialize the next layer of the packet. For example,
//!   given a buffer with a TCP segment already serialized in it, serialize the
//!   IPv4 header, resulting in an IPv4 packet containing a TCP segment.
//! - Second, given a description of a nested sequence of packets, figure out
//!   the constraints that a buffer must satisfy in order to be able to fit the
//!   entire sequence, and allocate a buffer which satisfies those constraints.
//!   This buffer is then used to serialize one layer at a time, as described in
//!   the previous bullet.
//!
//! ### Serializing into a buffer
//!
//! The `PacketBuilder` trait is implemented by types which are capable of
//! serializing a new layer of a packet into an existing buffer. For example, we
//! might define an `Ipv4PacketBuilder` type, which describes the source IP
//! address, destination IP address, and any other metadata required to generate
//! the header of an IPv4 packet. Importantly, a `PacketBuilder` does *not*
//! define any encapsulated packets. In order to construct a TCP segment in an
//! IPv4 packet, we would need a separate `TcpSegmentBuilder` to describe the
//! TCP segment.
//!
//! A `PacketBuilder` exposes the number of bytes it requires for headers,
//! footers, and minimum body lengths via the `header_len`, `footer_len`, and
//! `min_body_len` methods. It serializes via the `serialize` method.
//!
//! In order to serialize a `PacketBuilder`, a `SerializeBuffer` must first be
//! constructed. A `SerializeBuffer` is a view into a `Buffer` used for
//! serialization, and it is initialized with the proper number of bytes for the
//! header, footer, and body. The number of bytes required for these is
//! discovered through calls to the `PacketBuilder`'s `header_len`,
//! `footer_len`, and `min_body_len` methods.
//!
//! The `PacketBuilder`'s `serialize` method consumes the `PacketBuilder` itself
//! and the `SerializeBuffer` by value, and serializes the headers and footers
//! of the packet into the buffer. It expects that the `SerializeBuffer` is
//! initialized with a body equal to the body which will be encapsulated. For
//! example, imagine that we are trying to serialize a TCP segment in an IPv4
//! packet in an Ethernet frame, and that, so far, we have only serialized the
//! TCP segment:
//!
//! ```text
//! |-------------------------------------|++++++++++++++++++++|-----| TCP segment
//! |-----------------|++++++++++++++++++++++++++++++++++++++++|-----| IPv4 packet
//! |++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++| Ethernet frame
//!
//! |-------------------------------------|--------------------|-----|
//!                                             TCP segment
//!
//! |-------------------------------------|--------------------|-----|
//!              Buffer prefix                 Buffer body       Buffer suffix
//! ```
//!
//! Note that the buffer's body is currently equal to the TCP segment, and the
//! contents of the body are already initialized to the segment's contents.
//!
//! Given an `Ipv4PacketBuilder`, we call the appropriate methods to discover
//! that it requires 20 bytes for its header. Thus, we modify the buffer by
//! extending the body by 20 bytes, and constructing a `SerializeBuffer` whose
//! header references the newly-added 20 bytes, and whose body references the
//! old contents of the body, corresponding to the TCP segment.
//!
//! ```text
//! |-------------------------------------|++++++++++++++++++++|-----| TCP segment
//! |-----------------|++++++++++++++++++++++++++++++++++++++++|-----| IPv4 packet
//! |++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++| Ethernet frame
//!
//! |-----------------|-------------------|--------------------|-----|
//!                        IPv4 header          IPv4 body
//!                             |                   |
//!                             +-----------+       |
//!                                         |       |
//!                      SerializeBuffer { header, body }
//!
//! |-----------------|----------------------------------------|-----|
//!    Buffer prefix                 Buffer body                 Buffer suffix
//! ```
//!
//! We then pass the `SerializeBuffer` to a call to the `Ipv4PacketBuilder`'s
//! `serialize` method, and it serializes the IPv4 header in the space provided.
//! When the call to `serialize` returns, the `SerializeBuffer` and
//! `Ipv4PacketBuilder` have been discarded, and the buffer's body is now equal
//! to the bytes of the IPv4 packet.
//!
//! ```text
//! |-------------------------------------|++++++++++++++++++++|-----| TCP segment
//! |-----------------|++++++++++++++++++++++++++++++++++++++++|-----| IPv4 packet
//! |++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++| Ethernet frame
//!
//! |-----------------|----------------------------------------|-----|
//!                                  IPv4 packet
//!
//! |-----------------|----------------------------------------|-----|
//!    Buffer prefix                 Buffer body                 Buffer suffix
//! ```
//!
//! Now, we area ready to repeat the same process with the Ethernet layer of the
//! packet.
//!
//! ### Constructing a buffer for serialization
//!
//! Now that we know how, given a buffer with a subset of a packet serialized
//! into it, we can serialize the next layer of the packet, we need to figure
//! out how to construct such a buffer in the first place.
//!
//! The primary challenge here is that we need to be able to commit to what
//! we're going to serialize before we actually serialize it. For example,
//! consider sending a TCP segment to the network. From the perspective of the
//! TCP module of our code, we don't know how large the buffer needs to be
//! because don't know what packet layers our TCP segment will be encapsulated
//! inside of. If the IP layer decides to route our segment over an Ethernet
//! link, then we'll need to have a buffer large enough for a TCP segment in an
//! IPv4 packet in an Ethernet segment. If, on the other hand, the IP layer
//! decides to route our segment through a GRE tunnel, then we'll need to have a
//! buffer large enough for a TCP segment in an IPv4 packet in a GRE packet in
//! an IP packet in an Ethernet segment.
//!
//! We accomplish this commit-before-serializing via the `Serializer` trait. A
//! `Serializer` describes a packet which can be serialized in the future, but
//! which has not yet been serialized. Unlike a `PacketBuilder`, a `Serializer`
//! describes all layers of a packet up to a certain point. For example, a
//! `Serializer` might describe a TCP segment, or it might describe a TCP
//! segment in an IP packet, or it might describe a TCP segment in an IP packet
//! in an Ethernet frame, etc.
//!
//! #### Constructing a `Serializer`
//!
//! `Serializer`s are recursive - a `Serializer` combined with a `PacketBuilder`
//! yields a new `Serializer` which describes encapsulating the original
//! `Serializer` in a new packet layer. For example, a `Serializer` describing a
//! TCP segment combined with an `Ipv4PacketBuilder` yields a `Serializer` which
//! describes a TCP segment in an IPv4 packet. Concretely, given a `Serializer`,
//! `s`, and a `PacketBuilder`, `b`, a new `Serializer` can be constructed by
//! calling `s.encapsulate(b)`. The [`Serializer::encapsulate`] method consumes
//! both the `Serializer` and the `PacketBuilder` by value, and returns a new
//! `Serializer`.
//!
//! Note that, while `Serializer`s are passed around by value, they are only as
//! large in memory as the `PacketBuilder`s they're constructed from, and those
//! should, in most cases, be quite small. If size is a concern, the
//! `PacketBuilder` trait can be implemented for a reference type (e.g.,
//! `&Ipv4PacketBuilder`), and references passed around instead of values.
//!
//! #### Constructing a buffer from a `Serializer`
//!
//! If `Serializer`s are constructed by starting at the innermost packet layer
//! and working outwards, adding packet layers, then in order to turn a
//! `Serializer` into a buffer, they are consumed by starting at the outermost
//! packet layer and working inwards.
//!
//! In order to construct a buffer, the [`Serializer::serialize`] method is
//! provided. It takes a `Bar`, which describes the constraints built up by any
//! encapsulating layers. For example, when serializing a TCP segment in an IP
//! packet in an Ethernet frame, the `serialize` call on the IP packet
//! `Serializer` would be given a `Bar` describing the Ethernet frame's
//! requirement of 18 prefix bytes for its header. This call would then compute
//! a new `Bar` describing the combined requirement of 38 prefix bytes for the
//! IP and Ethernet headers combined, and would pass this to a call to
//! `serialize` on the TCP segment `Serializer`.
//!
//! When the innermost call to `serialize` is reached, it is that call's
//! responsibility to produce a buffer which satisfies the constraints passed to
//! it, and to initialize that buffer's body with the contents of its packet. For
//! example, the TCP segment `Serializer` from the preceding example would need
//! to produce a buffer with 38 bytes of prefix, and whose body was initialized
//! to the bytes of the TCP segment.
//!
//! We can now see how `Serializer`s and `PacketBuilder`s compose - the buffer
//! returned from a call to `serialize` satisfies the requirements of the
//! `PacketBuilder::serialize` method - its body is initialized to the packet to
//! be encapsulated, and enough prefix and suffix space exist to serialize this
//! layer's header and footer. For example, the call to `Serializer::serialize`
//! on the TCP segment serializer would return a buffer with 38 bytes of prefix
//! and a body initialized to the bytes of the TCP segment. The call to
//! `Serializer::serialize` on the IP packet would then pass this buffer to a
//! call to `PacketBuilder::serialize` on its `Ipv4PacketBuilder`, resulting in
//! a buffer with 18 bytes of prefix and a body initialized to the bytes of the
//! entire IP packet. This buffer would then be suitable to return from the call
//! to `Serializer::serialize`, allowing the Ethernet layer to continue
//! operating on the buffer, and so on.
//!
//! Note in particular that, throughout this entire process of constructing
//! `Serializer`s and `PacketBuilder`s and then consuming them, a buffer is only
//! allocated once, and each byte of the packet is only serialized once. No
//! temporary buffers or copying between buffers is required.
//!
//! #### Reusing buffers
//!
//! Another important property of the `Serializer` trait is that it can be
//! implemented by buffers. Since buffers contain prefixes, bodies, and
//! suffixes, and since the `Serializer::serialize` method consumes the
//! `Serializer` by value and returns a buffer by value, a buffer is itself a
//! valid `Serializer`. When `serialize` is called, so long as it already
//! satisfies the constraints requested, it can simply return itself by value.
//! If the constraints are not satisfied, it may need to produce a different
//! buffer through some user-defined mechanism. The [`BufferSerializer`] type
//! wraps an existing buffer and implements the `Serializer` trait as described
//! here.
//!
//! This allows existing buffers to be reused in many cases. For example,
//! consider receiving a packet in a buffer, and then responding to that packet
//! with a new packet. The buffer that the original packet was stored in can be
//! used to serialize the new packet, avoiding any unnecessary allocation.
//!
//! # Monomorphization Overflow
//!
//! Because this crate's APIs make heavy use of static type parameters,
//! recursive functions can sometimes run into a compiler issue known as
//! "monomorphization overflow". This section describes the problem, and
//! explains how to work around it.
//!
//! Monomorphization overflow happens when the program type checks, but the
//! compiler overflows its stack while trying to figure out what concrete type
//! to use for particular objects. It is guaranteed to happen whenever the
//! compiler tries to figure out what a type, `T`, is, where it knows that `T =
//! U<T>` for some `U`. Substituting `U`, we find that `T = U<U<T>>`, and thus
//! that `T = U<U<U<T>>>`, and so on. Eventually, the compiler simply gives up
//! (which is good, since there's no solution).
//!
//! In order to explain how this problem can arise by using this crate, consider
//! the following code:
//!
//! ```rust
//! # use packet::{ParseBuffer, Either};
//! fn foo<B: ParseBuffer>(buffer: B, n: usize) {
//!     if n == 0 {
//!         return;
//!     }
//!     foo::<Either<B, &[u8]>>(Either::A(buffer), n - 1);
//! }
//! ```
//!
//! If there were a call to `foo(&[][..], 10)`, that call would have `B =
//! &[u8]`. But it would contain a call to `foo` with `B' = Either<&[u8],
//! &[u8]>`, which would contain a call to `foo` with `B'' =
//! Either<Either<&[u8], &[u8]>, &[u8]>`, and so on. The compiler will
//! eventually give up on trying to figure out the concrete types of all of the
//! recursive calls to `foo`.
//!
//! ## Solution
//!
//! The solution is to break the cycle by introducing a concrete
//! (non-parametric) type. For this, we provide the [`ParseBuffer::as_buf`] and
//! [`ParseBufferMut::as_buf_mut`] methods. These methods return `Buf<&[u8]>`
//! and `Buf<&mut [u8]>` respectively. Since `Buf` is a concrete type, these
//! methods can be used to break the cycle. We can amend the example above:
//!
//! ```rust
//! # use packet::{Buf, ParseBuffer, Either};
//! fn foo<B: ParseBuffer>(buffer: B, n: usize) {
//!     if n == 0 {
//!         return;
//!     }
//!     foo::<Either<Buf<&[u8]>, &[u8]>>(Either::A(buffer.as_buf()), n - 1);
//! }
//! ```
//!
//! Now, the first call to `foo(&[][..], 10)` has `B = &[u8]`, and all
//! subsequent calls have `B' = Either<Buf<&[u8]>, &[u8]>`, and the program will
//! compile successfully.
//!
//! **WARNING**: Calling methods on the `Buf` from `as_buf` or `as_buf_mut` has
//! a small performance penalty when compared to calls on the original
//! `ParseBuffer`. They should only be used when necessary to avoid compilation
//! failure. If there are multiple options for where to put the `as_buf` or
//! `as_buf_mut` call in order to break the cycle, pick the option which is the
//! least in the hot path.

extern crate zerocopy;

pub mod serialize;

pub use crate::serialize::*;

use std::cmp;
use std::mem;
use std::ops::{Bound, Range, RangeBounds};

use zerocopy::{ByteSlice, ByteSliceMut, LayoutVerified, Unaligned};

/// A byte buffer used for parsing.
///
/// A `ParseBuffer` is a [`Buffer`] without the guarantee that the prefix or
/// suffix will be retained. This is sufficient for parsing, but not for
/// serialization. The `ParseBuffer` trait defines all of the methods which do
/// not require this guarantee. `Buffer` extends `ParseBuffer`.
///
/// While a `ParseBuffer` allows the ranges covered by its prefix, body, and
/// suffix to be modified, it only provides immutable access to their contents.
/// For mutable access, see [`ParseBufferMut`].
///
/// # Notable implementations
///
/// `ParseBuffer` is implemented for byte slices - `&[u8]` and `&mut [u8]`.
/// These types do not implement `Buffer`; once bytes are consumed from their
/// bodies, those bytes are discarded and cannot be recovered.
pub trait ParseBuffer: AsRef<[u8]> {
    /// The length of the body.
    fn len(&self) -> usize {
        self.as_ref().len()
    }

    /// Is the body empty?
    fn is_empty(&self) -> bool {
        self.len() == 0
    }

    /// Shrinks the front of the body towards the end of the buffer.
    ///
    /// `shrink_front` consumes the `n` left-most bytes of the body, and adds
    /// them to the prefix.
    ///
    /// # Panics
    ///
    /// `shrink_front` panics if `n` is larger than the body.
    fn shrink_front(&mut self, n: usize);

    /// Shrinks the buffer to be no larger than `len` bytes, consuming from the
    /// front.
    ///
    /// `shrink_front_to` consumes as many of the left-most bytes of the body as
    /// necessary to ensure that the buffer is no larger than `len` bytes long.
    /// It adds any bytes consumed to the prefix. If the buffer is already `len`
    /// bytes long or shorter, `shrink_front_to` does nothing.
    fn shrink_front_to(&mut self, len: usize) {
        let old_len = self.len();
        let new_len = cmp::min(old_len, len);
        self.shrink_front(old_len - new_len);
    }

    /// Shrinks the back of the body towards the beginning of the buffer.
    ///
    /// `shrink_back` consumes the `n` right-most bytes of the body, and adds
    /// them to the suffix.
    ///
    /// # Panics
    ///
    /// `shrink_back` panics if `n` is larger than the body.
    fn shrink_back(&mut self, n: usize);

    /// Shrinks the buffer to be no larger than `len` bytes, consuming from the
    /// back.
    ///
    /// `shrink_back_to` consumes as many of the right-most bytes of the body as
    /// necessary to ensure that the buffer is no larger than `len` bytes long.
    /// It adds any bytes consumed to the suffix. If the buffer is already `len`
    /// bytes long or shorter, `shrink_back_to` does nothing.
    fn shrink_back_to(&mut self, len: usize) {
        let old_len = self.len();
        let new_len = cmp::min(old_len, len);
        self.shrink_back(old_len - new_len);
    }

    /// Shrinks the body.
    ///
    /// `shrink` shrinks the body to be equal to `range` of the previous body.
    /// Any bytes preceding the range are added to the prefix, and any bytes
    /// following the range are added to the suffix.
    ///
    /// # Panics
    ///
    /// `shrink` panics if `range` is out of bounds of the body, or if the range
    /// is nonsensical (the end precedes the start).
    fn shrink<R: RangeBounds<usize>>(&mut self, range: R) {
        let len = self.len();
        let range = canonicalize_range(len, &range);
        self.shrink_front(range.start);
        self.shrink_back(len - range.end);
    }

    /// Parses a packet from the body.
    ///
    /// `parse` parses a packet from the body by invoking [`P::parse`] on a
    /// [`BufferView`] into this buffer. Any bytes consumed from the
    /// `BufferView` are also consumed from the body, and added to the prefix or
    /// suffix. After `parse` has returned, the buffer's body will contain only
    /// those bytes which were not consumed by the call to `P::parse`.
    ///
    /// See the [`BufferView`] and [`ParsablePacket`] documentation for more
    /// details.
    ///
    /// [`P::parse`]: ParsablePacket::parse
    fn parse<'a, P: ParsablePacket<&'a [u8], ()>>(&'a mut self) -> Result<P, P::Error> {
        self.parse_with(())
    }

    /// Parses a packet with arguments.
    ///
    /// `parse_with` is like `parse`, but it accepts arguments to pass to
    /// [`P::parse`].
    ///
    /// [`P::parse`]: ParsablePacket::parse
    fn parse_with<'a, ParseArgs, P: ParsablePacket<&'a [u8], ParseArgs>>(
        &'a mut self,
        args: ParseArgs,
    ) -> Result<P, P::Error>;

    /// Creates a `Buf` of a byte slice which is equivalent to this buffer.
    ///
    /// This is useful to avoid monomorphization overflow. For details, see the
    /// "Monomorphization Overflow" section of the crate documentation.
    fn as_buf(&self) -> Buf<&[u8]>;
}

/// A `ParseBuffer` which provides mutable access to its contents.
///
/// While a `ParseBuffer` allows the ranges covered by its prefix, body, and
/// suffix to be modified, it only provides immutable access to their contents.
/// A `ParseBufferMut`, on the other hand, provides mutable access to the
/// contents of its prefix, body, and suffix.
///
/// # Notable implementations
///
/// `ParseBufferMut` is implemented for mutable byte slices - `&mut [u8]`.
/// Mutable byte slices do not implement `Buffer` or `BufferMut`; once bytes are
/// consumed from their bodies, those bytes are discarded and cannot be
/// recovered.
pub trait ParseBufferMut: ParseBuffer + AsMut<[u8]> {
    /// Parses a mutable packet from the body.
    ///
    /// `parse_mut` is like [`ParseBuffer::parse`], but instead of calling
    /// [`P::parse`] on a [`BufferView`], it calls [`P::parse_mut`] on a
    /// [`BufferViewMut`]. The effect is that the parsed packet can contain
    /// mutable references to the buffer. This can be useful if you want to
    /// modify parsed packets in-place.
    ///
    /// Depending on the implementation of [`P::parse_mut`], the contents
    /// of the buffer may be modified during parsing.
    ///
    /// See the [`BufferViewMut`] and [`ParsablePacket`] documentation for more
    /// details.
    ///
    /// [`P::parse`]: ParsablePacket::parse
    /// [`P::parse_mut`]: ParsablePacket::parse_mut
    fn parse_mut<'a, P: ParsablePacket<&'a mut [u8], ()>>(&'a mut self) -> Result<P, P::Error> {
        self.parse_with_mut(())
    }

    /// Parses a mutable packet with arguments.
    ///
    /// `parse_with_mut` is like `parse_mut`, but it accepts arguments to pass
    /// to [`P::parse_mut`].
    ///
    /// [`P::parse_mut`]: ParsablePacket::parse_mut
    fn parse_with_mut<'a, ParseArgs, P: ParsablePacket<&'a mut [u8], ParseArgs>>(
        &'a mut self,
        args: ParseArgs,
    ) -> Result<P, P::Error>;

    /// Creates a `Buf` of a mutable byte slice which is equivalent to this
    /// buffer.
    ///
    /// This is useful to avoid monomorphization overflow. For details, see the
    /// "Monomorphization Overflow" section of the crate documentation.
    fn as_buf_mut(&mut self) -> Buf<&mut [u8]>;
}

/// A byte buffer used for parsing and serialization.
///
/// A `Buffer` is a byte buffer with a prefix, a body, and a suffix. The size of
/// the buffer is referred to as its "capacity", and the size of the body is
/// referred to as its "length". The body of the buffer can shrink or grow as
/// allowed by the capacity as packets are parsed or serialized. `Buffer`'s
/// `AsRef<[u8]>` implementation provides access to the body.
///
/// A `Buffer` guarantees never to discard bytes from the prefix or suffix,
/// which is an important requirement for serialization. \[1\] For parsing, this
/// guarantee is not needed. The subset of methods which do not require this
/// guarantee are defined in the `ParseBuffer` trait, which does not have this
/// requirement. `Buffer` extends `ParseBuffer`.
///
/// While a `Buffer` allows the ranges covered by its prefix, body, and suffix
/// to be modified, it only provides immutable access to their contents. For
/// mutable access, see [`BufferMut`].
///
/// \[1\] If `Buffer`s could shrink their prefix or suffix, then it would not be
/// possible to guarantee that a call to `undo_parse` wouldn't panic.
/// `undo_parse` is used when retaining previously-parsed packets for
/// serialization, which is useful in scenarios such as packet forwarding.
pub trait Buffer: ParseBuffer {
    /// The capacity of the buffer.
    ///
    /// `b.capacity()` is equivalent to `b.prefix_len() + b.len() + b.suffix_len()`.
    fn capacity(&self) -> usize {
        self.prefix_len() + self.len() + self.suffix_len()
    }

    /// The length of the prefix.
    fn prefix_len(&self) -> usize;

    /// The length of the suffix.
    fn suffix_len(&self) -> usize;

    /// Grows the front of the body towards the beginning of the buffer.
    ///
    /// `grow_front` consumes the right-most `n` bytes of the prefix, and adds
    /// them to the body.
    ///
    /// # Panics
    ///
    /// `grow_front` panics if `n` is larger than the prefix.
    fn grow_front(&mut self, n: usize);

    /// Grows the back of the body towards the end of the buffer.
    ///
    /// `grow_back` consumes the left-most `n` bytes of the suffix, and adds
    /// them to the body.
    ///
    /// # Panics
    ///
    /// `grow_back` panics if `n` is larger than the suffix.
    fn grow_back(&mut self, n: usize);

    /// Resets the body to be equal to the entire buffer.
    ///
    /// `reset` consumes the entire prefix and suffix, adding them to the body.
    fn reset(&mut self) {
        // TODO(joshlf): Inline these calls once we have NLL.
        let prefix = self.prefix_len();
        let suffix = self.suffix_len();
        self.grow_front(prefix);
        self.grow_back(suffix);
    }

    /// Undo the effects of a previous parse in preparation for serialization.
    ///
    /// `undo_parse` undoes the effects of having previously parsed a packet by
    /// consuming the appropriate number of bytes from the prefix and suffix.
    /// After a call to `undo_parse`, the buffer's body will contain the bytes
    /// of the previously-parsed packet. This allows a previously-parsed packet
    /// to be used in serialization.
    ///
    /// `undo_parse` takes a `ParseMetadata`, which can be obtained from
    /// [`ParsablePacket::parse_metadata`].
    ///
    /// `undo_parse` must always be called starting with the most recently
    /// parsed packet, followed by the second most recently parsed packet, and
    /// so on. Otherwise, it may panic, and in any case, almost certainly won't
    /// produce the desired buffer contents.
    ///
    /// # Panics
    ///
    /// `undo_parse` may panic if called in the wrong order. See the previous
    /// paragraph.
    fn undo_parse(&mut self, meta: ParseMetadata) {
        if self.len() < meta.body_len {
            // There were padding bytes which were stripped when parsing the
            // encapsulated packet. We need to add them back in order to restore
            // the original packet.
            let len = self.len();
            self.grow_back(meta.body_len - len);
        }
        self.grow_front(meta.header_len);
        self.grow_back(meta.footer_len);
    }
}

/// A `Buffer` which provides mutable access to its contents.
///
/// While a `Buffer` allows the ranges covered by its prefix, body, and suffix
/// to be modified, it only provides immutable access to their contents. A
/// `BufferMut`, on the other hand, provides mutable access to the contents of
/// its prefix, body, and suffix.
pub trait BufferMut: Buffer + ParseBufferMut {
    /// Extends the front of the body towards the beginning of the buffer,
    /// zeroing the new bytes.
    ///
    /// `grow_front_zero` calls [`Buffer::grow_front`] and sets the
    /// newly-added bytes to 0. This can be useful when serializing to ensure
    /// that the contents of packets previously stored in the buffer are not
    /// leaked.
    fn grow_front_zero(&mut self, n: usize) {
        self.grow_front(n);
        zero(&mut self.as_mut()[..n])
    }

    /// Extends the back of the body towards the end of the buffer, zeroing the
    /// new bytes.
    ///
    /// `grow_back_zero` calls [`Buffer::grow_back`] and sets the
    /// newly-added bytes to 0. This can be useful when serializing to ensure
    /// that the contents of packets previously stored in the buffer are not
    /// leaked.
    fn grow_back_zero(&mut self, n: usize) {
        let old_len = self.len();
        self.grow_back(n);
        zero(&mut self.as_mut()[old_len..]);
    }

    /// Resets the body to be equal to the entire buffer, zeroing the new bytes.
    ///
    /// Like [`Buffer::reset`], `reset_zero` consumes the entire prefix and
    /// suffix, adding them to the body. It sets these bytes to 0. This can be
    /// useful when serializing to ensure that the contents of packets
    /// previously stored in the buffer are not leaked.
    fn reset_zero(&mut self) {
        // TODO(joshlf): Inline these calls once we have NLL.
        let prefix = self.prefix_len();
        let suffix = self.suffix_len();
        self.grow_front_zero(prefix);
        self.grow_back_zero(suffix);
    }

    /// Serializes a packet in the buffer.
    ///
    /// `serialize` serializes the packet described in `builder` into the
    /// buffer. The body of the buffer is used as the body of the packet, and
    /// the prefix and suffix of the buffer are used to serialize the packet's
    /// header and footer. This is a low-level function: you probably want the
    /// [`Serializer`] trait instead.
    ///
    /// If `builder` has a minimum body size which is larger than the current
    /// body, the body is first grown to the right (towards the end of the
    /// buffer) with padding bytes in order to meet the minimum body size. This
    /// is transparent to the `builder` - it always just sees a body which meets
    /// the minimum body size requirement.
    ///
    /// The added padding is zeroed in order to avoid leaking the contents of
    /// packets previously stored in the buffer.
    ///
    /// # Panics
    ///
    /// `serialize` panics if there are not enough prefix or suffix bytes to
    /// serialize the packet. In particular, `b.serialize(buffer)` panics if
    /// either of the following hold:
    /// - `b.prefix_len() < buffer.header_bytes()`
    /// - `b.len() + b.suffix_len() < buffer.min_body_bytes() +
    ///   buffer.footer_bytes()`
    fn serialize<B: PacketBuilder>(&mut self, builder: B) {
        if self.len() < builder.min_body_len() {
            // The body isn't large enough to satisfy the minimum body length
            // requirement, so we add padding.

            // SECURITY: Use _zero to ensure we zero padding bytes to prevent
            // leaking information from packets previously stored in this
            // buffer.
            let len = self.len();
            self.grow_back_zero(builder.min_body_len() - len);
        }

        let body_len = self.len();
        // These aren't necessary for correctness (grow_xxx_zero will panic
        // under the same conditions that these assertions will fail), but they
        // provide nicer error messages for debugging.
        debug_assert!(
            self.prefix_len() >= builder.header_len(),
            "prefix ({} bytes) too small to serialize header ({} bytes)",
            self.prefix_len(),
            builder.header_len()
        );
        debug_assert!(
            self.suffix_len() >= builder.footer_len(),
            "suffix ({} bytes) too small to serialize footer ({} bytes)",
            self.suffix_len(),
            builder.footer_len()
        );
        // SECURITY: _zero here is technically unnecessary since it's
        // PacketBuilder::serialize's responsibility to zero/initialize the
        // header and footer, but we do it anyway to hedge against non-compliant
        // PacketBuilder::serialize implementations. If this becomes a
        // performance issue, we can revisit it, but the optimizer will probably
        // take care of it for us.
        self.grow_front_zero(builder.header_len());
        self.grow_back_zero(builder.footer_len());

        let body = builder.header_len()..(builder.header_len() + body_len);
        builder.serialize(SerializeBuffer { buf: self.as_mut(), body });
    }
}

/// A view into a `Buffer`.
///
/// A `BufferView` borrows a `Buffer`, and provides methods to consume bytes
/// from the buffer's body. It is primarily intended to be used for parsing,
/// although it provides methods which are useful for serialization as well.
///
/// `BufferView` only provides immutable access to the contents of the buffer.
/// For mutable access, see [`BufferViewMut`].
///
/// # Notable implementations
///
/// `BufferView` is implemented for mutable references to byte slices (`&mut
/// &[u8]` and `&mut &mut [u8]`).
pub trait BufferView<B: ByteSlice>: Sized + AsRef<[u8]> {
    /// The length of the buffer's body.
    fn len(&self) -> usize {
        self.as_ref().len()
    }

    /// Is the buffer's body empty?
    fn is_empty(&self) -> bool {
        self.len() == 0
    }

    /// Takes `n` bytes from the front of the buffer's body.
    ///
    /// `take_front` consumes `n` bytes from the front of the buffer's body.
    /// After a successful call to `take_front(n)`, the prefix is `n` bytes
    /// longer and the body is `n` bytes shorter. If the body is not at least
    /// `n` bytes in length, `take_front` returns `None`.
    fn take_front(&mut self, n: usize) -> Option<B>;

    /// Takes `n` bytes from the back of the buffer's body.
    ///
    /// `take_back` consumes `n` bytes from the back of the buffer's body. After
    /// a successful call to `take_back(n)`, the suffix is `n` bytes longer and
    /// the body is `n` bytes shorter. If the body is not at least `n` bytes in
    /// length, `take_back` returns `None`.
    fn take_back(&mut self, n: usize) -> Option<B>;

    /// Takes the rest of the buffer's body from the front.
    ///
    /// `take_rest_front` consumes the rest of the bytes from the buffer's body.
    /// After a call to `take_rest_front`, the body is empty, and the bytes
    /// which were previously in the body are now in the prefix.
    fn take_rest_front(mut self) -> B {
        let len = self.len();
        self.take_front(len).unwrap()
    }

    /// Takes the rest of the buffer's body from the back.
    ///
    /// `take_rest_back` consumes the rest of the bytes from the buffer's body.
    /// After a call to `take_rest_back`, the body is empty, and the bytes which
    /// were previously in the body are now in the suffix.
    fn take_rest_back(mut self) -> B {
        let len = self.len();
        self.take_back(len).unwrap()
    }

    /// Converts this view into a reference to the buffer's body.
    ///
    /// `into_rest` consumes this `BufferView` by value, and returns a reference
    /// to the buffer's body. Unlike `take_rest`, the body is not consumed - it
    /// is left unchanged.
    fn into_rest(self) -> B;

    /// Takes an object from the front of the buffer's body.
    ///
    /// `take_obj_front` consumes `size_of::<T>` bytes from the front of the
    /// buffer's body, and interprets them as a `T`. After a successful call to
    /// `take_obj_front::<T>()`, the prefix is `size_of::<T>()` bytes longer and
    /// the body is `size_of::<T>()` bytes shorter. If the body is not at least
    /// `size_of::<T>()` bytes in length, `take_obj_front` returns `None`.
    fn take_obj_front<T>(&mut self) -> Option<LayoutVerified<B, T>>
    where
        T: Unaligned,
    {
        let bytes = self.take_front(mem::size_of::<T>())?;
        // new_unaligned only returns None if there aren't enough bytes
        Some(LayoutVerified::new_unaligned(bytes).unwrap())
    }

    /// Takes a slice of objects from the front of the buffer's body.
    ///
    /// `take_slice_front` consumes `n * size_of::<T>()` bytes from the front of
    /// the buffer's body, and interprets them as a `[T]` with `n` elements.
    /// After a successful call to `take_slice_front::<T>()`, the prefix is
    /// `n * size_of::<T>()` bytes longer and the body is `n * size_of::<T>()`
    /// bytes shorter. If the body is not at least `n * size_of::<T>()` bytes in
    /// length, `take_slice_front` returns `None`.
    ///
    /// # Panics
    ///
    /// `take_slice_front` panics if `T` is a zero-sized type.
    fn take_slice_front<T>(&mut self, n: usize) -> Option<LayoutVerified<B, [T]>>
    where
        T: Unaligned,
    {
        let bytes = self.take_front(n * mem::size_of::<T>())?;
        // `new_slice_unaligned` will return `None` only if `bytes.len()` is
        // not a multiple of `mem::size_of::<T>()`.
        Some(LayoutVerified::new_slice_unaligned(bytes).unwrap())
    }

    /// Takes an object from the back of the buffer's body.
    ///
    /// `take_obj_back` consumes `size_of::<T>` bytes from the back of the
    /// buffer's body, and interprets them as a `T`. After a successful call to
    /// `take_obj_back::<T>()`, the suffix is `size_of::<T>()` bytes longer and
    /// the body is `size_of::<T>()` bytes shorter. If the body is not at least
    /// `size_of::<T>()` bytes in length, `take_obj_back` returns `None`.
    fn take_obj_back<T>(&mut self) -> Option<LayoutVerified<B, T>>
    where
        T: Unaligned,
    {
        let bytes = self.take_back(mem::size_of::<T>())?;
        // new_unaligned only returns None if there aren't enough bytes
        Some(LayoutVerified::new_unaligned(bytes).unwrap())
    }

    /// Takes a slice of objects from the back of the buffer's body.
    ///
    /// `take_slice_back` consumes `n * size_of::<T>()` bytes from the back of
    /// the buffer's body, and interprets them as a `[T]` with `n` elements.
    /// After a successful call to `take_slice_back::<T>()`, the suffix is
    /// `n * size_of::<T>()` bytes longer and the body is `n * size_of::<T>()`
    /// bytes shorter. If the body is not at least `n * size_of::<T>()` bytes in
    /// length, `take_slice_back` returns `None`.
    ///
    /// # Panics
    ///
    /// `take_slice_back` panics if `T` is a zero-sized type.
    fn take_slice_back<T>(&mut self, n: usize) -> Option<LayoutVerified<B, [T]>>
    where
        T: Unaligned,
    {
        let bytes = self.take_back(n * mem::size_of::<T>())?;
        // `new_slice_unaligned` will return `None` only if `bytes.len()` is
        // not a multiple of `mem::size_of::<T>()`.
        Some(LayoutVerified::new_slice_unaligned(bytes).unwrap())
    }
}

/// A mutable view into a `Buffer`.
///
/// A `BufferViewMut` is a `BufferView` which provides mutable access to the
/// contents of the buffer.
///
/// # Notable implementations
///
/// `BufferViewMut` is implemented for `&mut &mut [u8]`.
pub trait BufferViewMut<B: ByteSliceMut>: BufferView<B> + AsMut<[u8]> {
    /// Takes `n` bytes from the front of the buffer's body and zeroes them.
    ///
    /// `take_front_zero` is like [`BufferView::take_front`], except that it
    /// zeroes the bytes before returning them. This can be useful when
    /// serializing to ensure that the contents of packets previously stored in
    /// the buffer are not leaked.
    fn take_front_zero(&mut self, n: usize) -> Option<B> {
        self.take_front(n).map(|mut buf| {
            zero(buf.deref_mut());
            buf
        })
    }

    /// Takes `n` bytes from the back of the buffer's body and zeroes them.
    ///
    /// `take_back_zero` is like [`BufferView::take_back`], except that it
    /// zeroes the bytes before returning them. This can be useful when
    /// serializing to ensure that the contents of packets previously stored in
    /// the buffer are not leaked.
    fn take_back_zero(&mut self, n: usize) -> Option<B> {
        self.take_back(n).map(|mut buf| {
            zero(buf.deref_mut());
            buf
        })
    }

    /// Takes the rest of the buffer's body from the front and zeroes it.
    ///
    /// `take_rest_front_zero` is like [`BufferView::take_rest_front`], except
    /// that it zeroes the bytes before returning them. This can be useful when
    /// serializing to ensure that the contents of packets previously stored in
    /// the buffer are not leaked.
    fn take_rest_front_zero(mut self) -> B {
        let len = self.len();
        self.take_front_zero(len).unwrap()
    }

    /// Takes the rest of the buffer's body from the back and zeroes it.
    ///
    /// `take_rest_back_zero` is like [`BufferView::take_rest_back`], except
    /// that it zeroes the bytes before returning them. This can be useful when
    /// serializing to ensure that the contents of packets previously stored in
    /// the buffer are not leaked.
    fn take_rest_back_zero(mut self) -> B {
        let len = self.len();
        self.take_front_zero(len).unwrap()
    }

    /// Converts this view into a reference to the buffer's body, and zero it.
    ///
    /// `into_rest_zero` is like [`BufferView::into_rest`], except that it
    /// zeroes the bytes before returning them. This can be useful when
    /// serializing to ensure that the contents of packets previously stored in
    /// the buffer are not leaked.
    fn into_rest_zero(self) -> B {
        let mut bytes = self.into_rest();
        zero(&mut bytes);
        bytes
    }

    /// Takes an object from the front of the buffer's body and zeroes it.
    ///
    /// `take_obj_front_zero` is like [`BufferView::take_obj_front`], except
    /// that it zeroes the bytes before converting them to a `T`. This can be
    /// useful when serializing to ensure that the contents of packets
    /// previously stored in the buffer are not leaked.
    fn take_obj_front_zero<T>(&mut self) -> Option<LayoutVerified<B, T>>
    where
        T: Unaligned,
    {
        let bytes = self.take_front(mem::size_of::<T>())?;
        // new_unaligned_zeroed only returns None if there aren't enough bytes
        Some(LayoutVerified::new_unaligned_zeroed(bytes).unwrap())
    }

    /// Takes an object from the back of the buffer's body and zeroes it.
    ///
    /// `take_obj_back_zero` is like [`BufferView::take_obj_back`], except that
    /// it zeroes the bytes before converting them to a `T`. This can be useful
    /// when serializing to ensure that the contents of packets previously
    /// stored in the buffer are not leaked.
    fn take_obj_back_zero<T>(&mut self) -> Option<LayoutVerified<B, T>>
    where
        T: Unaligned,
    {
        let bytes = self.take_back(mem::size_of::<T>())?;
        // new_unaligned_zeroed only returns None if there aren't enough bytes
        Some(LayoutVerified::new_unaligned_zeroed(bytes).unwrap())
    }
}

// NOTE on undo_parse algorithm: It's important that ParseMetadata only describe
// the packet itself, and not any padding. This is because the user might call
// undo_parse on a packet only once, and then serialize that packet inside of
// another packet with a lower minimum body length requirement than the one it
// was encapsulated in during parsing. In this case, if we were to include
// padding, we would spuriously serialize an unnecessarily large body. Omitting
// the padding is required for this reason. It is acceptable because, using the
// body_len field of the encapsulating packet's ParseMetadata, it is possible
// for undo_parse to reconstruct how many padding bytes there were if it needs
// to.
//
// undo_parse also needs to differentiate between bytes which were consumed from
// the beginning and end of the buffer. For normal packets this is easy -
// headers are consumed from the beginning, and footers from the end. For inner
// packets, which do not have a header/footer distinction (at least from the
// perspective of this crate), we arbitrarily decide that all bytes are consumed
// from the beginning. So long as ParsablePacket implementations obey this
// requirement, undo_parse will work properly. In order to support this,
// ParseMetadata::from_inner_packet constructs a ParseMetadata in which the only
// non-zero field is header_len.

/// Metadata about a previously-parsed packet used to undo its parsing.
///
/// See [`Buffer::undo_parse`] for more details.
#[derive(Copy, Clone)]
pub struct ParseMetadata {
    header_len: usize,
    body_len: usize,
    footer_len: usize,
}

impl ParseMetadata {
    pub fn from_packet(header_len: usize, body_len: usize, footer_len: usize) -> ParseMetadata {
        ParseMetadata { header_len, body_len, footer_len }
    }

    pub fn from_inner_packet(len: usize) -> ParseMetadata {
        ParseMetadata { header_len: len, body_len: 0, footer_len: 0 }
    }

    /// Gets the header length.
    ///
    /// `header_len` returns the length of the header of the packet described by
    /// this `ParseMetadata`.
    pub fn header_len(&self) -> usize {
        self.header_len
    }

    /// Gets the body length.
    ///
    /// `body_len` returns the length of the body of the packet described by
    /// this `ParseMetadata`.
    pub fn body_len(&self) -> usize {
        self.body_len
    }

    /// Gets the footer length.
    ///
    /// `footer_len` returns the length of the footer of the packet described by
    /// this `ParseMetadata`.
    pub fn footer_len(&self) -> usize {
        self.footer_len
    }
}

/// A packet which can be parsed from a `Buffer`.
///
/// A `ParsablePacket` is a packet which can be parsed from the body of a
/// buffer. It is recommended that as much of the packet object as possible be
/// references into the buffer in order to avoid copying for performance
/// reasons.
pub trait ParsablePacket<B: ByteSlice, ParseArgs>: Sized {
    /// The type of errors returned from `parse` and `parse_mut`.
    type Error;

    /// Parse a packet from a `Buffer`.
    ///
    /// Given a view into a `Buffer`, `parse` parses a packet by consuming bytes
    /// from the buffer's body. This works slightly differently for normal
    /// packets and inner packets (which do not contain other packets).
    ///
    /// ## Packets
    ///
    /// When parsing packets which contain other packets, the packet's headers
    /// and footers should be consumed from the beginning and end of the
    /// buffer's body respectively. The packet's body should be constructed from
    /// a reference to the buffer's body (i.e., `BufferView::into_body`), but
    /// the buffer's body should not be consumed. This allows the next
    /// encapsulated packet to be parsed from the remaining buffer body. See the
    /// crate documentation for more details.
    ///
    /// ## Inner Packets
    ///
    /// When parsing packets which do not contain other packets, the entire
    /// packet's contents should be consumed from the beginning of the buffer's
    /// body. The buffer's body should be empty after `parse` has returned.
    ///
    /// # Padding
    ///
    /// There may be post-packet padding which was added in order to satisfy the
    /// minimum body length requirement of a lower layer packet. If this packet
    /// describes its own length (and thus, it's possible to determine whether
    /// there's any padding), `parse` is required to consume any padding. If
    /// this invariant is not upheld, future calls to `Buffer::parse` or
    /// `Buffer::undo_parse` may behave incorrectly.
    fn parse<BV: BufferView<B>>(buffer: BV, args: ParseArgs) -> Result<Self, Self::Error>;

    /// Parse a packet from a `BufferMut`.
    ///
    /// `parse_mut` is like `parse`, except that it operates on a view into a
    /// `BufferMut` rather than a `Buffer`.
    fn parse_mut<BV: BufferViewMut<B>>(buffer: BV, args: ParseArgs) -> Result<Self, Self::Error>
    where
        B: ByteSliceMut,
    {
        Self::parse(buffer, args)
    }

    /// Metadata about this packet required by [`Buffer::undo_parse`].
    fn parse_metadata(&self) -> ParseMetadata;
}

/// A view into a `BufferMut` used for serializing new packets.
///
/// A `SerializeBuffer` is a view into a `BufferMut` which is used by the
/// [`PacketBuilder::serialize`] method to serialize a new packet. It is
/// constructed by the [`BufferMut::serialize`] method.
///
/// A `SerializeBuffer` provides separate access to the bytes which will store
/// the header, body, and footer of the new packet. The body is initialized to
/// contain the bytes of the packet to be encapsulated (including any padding),
/// and it is the caller's responsibility to serialize the header and footer.
///
/// `SerializeBuffer` implements `AsRef<[u8]>` and `AsMut<[u8]>`, providing a
/// reference to the entire buffer (header, body, and footer) at once.
pub struct SerializeBuffer<'a> {
    buf: &'a mut [u8],
    body: Range<usize>,
}

impl<'a> SerializeBuffer<'a> {
    /// Constructs a new `SerializeBuffer`.
    ///
    /// `new` constructs a new `SerializeBuffer`. The entire buffer is
    /// initialized to `buf`. The body is set to the subset of `buf` identified
    /// by the range `body`. The header is set to the subset of `buf` preceding
    /// the body, and the footer is set to the subset of `buf` following the
    /// body.
    ///
    /// # Panics
    ///
    /// `new` panics if `body` is out of range of `buf`, or if the range is
    /// nonsensical (the end precedes the start).
    pub fn new<R: RangeBounds<usize>>(buf: &'a mut [u8], body: R) -> SerializeBuffer<'a> {
        let body = canonicalize_range(buf.len(), &body);
        SerializeBuffer { buf, body }
    }

    /// Gets the bytes of the header.
    pub fn header(&mut self) -> &mut [u8] {
        &mut self.buf[..self.body.start]
    }

    /// Gets the bytes of the body.
    pub fn body(&mut self) -> &mut [u8] {
        &mut self.buf[self.body.clone()]
    }

    /// Gets the bytes of the footer.
    pub fn footer(&mut self) -> &mut [u8] {
        &mut self.buf[self.body.end..]
    }

    /// Gets the bytes of the header, body, and footer.
    ///
    /// `parts` gets references to the header, body, and footer all at once.
    /// Because of lifetime rules and the fact that the `header`, `body`, and
    /// `footer` methods borrow this `SerializeBuffer`, this is the only way to
    /// construct and operate on references to more than one section of the
    /// buffer at a time.
    pub fn parts(&mut self) -> (&mut [u8], &mut [u8], &mut [u8]) {
        let buf = &mut self.buf;
        let (prefix, rest) = buf.split_at_mut(self.body.start);
        let (body, suffix) = rest.split_at_mut(self.body.end - self.body.start);
        (prefix, body, suffix)
    }
}

impl<'a> AsRef<[u8]> for SerializeBuffer<'a> {
    fn as_ref(&self) -> &[u8] {
        self.buf
    }
}

impl<'a> AsMut<[u8]> for SerializeBuffer<'a> {
    fn as_mut(&mut self) -> &mut [u8] {
        self.buf
    }
}

fn zero(bytes: &mut [u8]) {
    for byte in bytes.iter_mut() {
        *byte = 0;
    }
}

impl<'a> ParseBuffer for &'a [u8] {
    fn len(&self) -> usize {
        <[u8]>::len(self)
    }
    fn shrink_front(&mut self, n: usize) {
        take_front(self, n);
    }
    fn shrink_back(&mut self, n: usize) {
        take_back(self, n);
    }
    fn parse_with<'b, ParseArgs, P: ParsablePacket<&'b [u8], ParseArgs>>(
        &'b mut self,
        args: ParseArgs,
    ) -> Result<P, P::Error> {
        P::parse(self, args)
    }
    fn as_buf(&self) -> Buf<&[u8]> {
        Buf::new(self, ..)
    }
}

impl<'a> ParseBuffer for &'a mut [u8] {
    fn len(&self) -> usize {
        <[u8]>::len(self)
    }
    fn shrink_front(&mut self, n: usize) {
        take_front_mut(self, n);
    }
    fn shrink_back(&mut self, n: usize) {
        take_back_mut(self, n);
    }
    fn parse_with<'b, ParseArgs, P: ParsablePacket<&'b [u8], ParseArgs>>(
        &'b mut self,
        args: ParseArgs,
    ) -> Result<P, P::Error> {
        P::parse(self, args)
    }

    fn as_buf(&self) -> Buf<&[u8]> {
        Buf::new(self, ..)
    }
}

impl<'a> ParseBufferMut for &'a mut [u8] {
    fn parse_with_mut<'b, ParseArgs, P: ParsablePacket<&'b mut [u8], ParseArgs>>(
        &'b mut self,
        args: ParseArgs,
    ) -> Result<P, P::Error> {
        P::parse_mut(self, args)
    }
    fn as_buf_mut(&mut self) -> Buf<&mut [u8]> {
        Buf::new(self, ..)
    }
}

impl<'b, 'a: 'b> BufferView<&'b [u8]> for &'b mut &'a [u8] {
    fn len(&self) -> usize {
        <[u8]>::len(self)
    }
    fn take_front(&mut self, n: usize) -> Option<&'b [u8]> {
        if self.len() < n {
            return None;
        }
        Some(take_front(self, n))
    }
    fn take_back(&mut self, n: usize) -> Option<&'b [u8]> {
        if self.len() < n {
            return None;
        }
        Some(take_back(self, n))
    }
    fn into_rest(self) -> &'b [u8] {
        self
    }
}

impl<'b, 'a: 'b> BufferView<&'b [u8]> for &'b mut &'a mut [u8] {
    fn len(&self) -> usize {
        <[u8]>::len(self)
    }
    fn take_front(&mut self, n: usize) -> Option<&'b [u8]> {
        if <[u8]>::len(self) < n {
            return None;
        }
        Some(take_front_mut(self, n))
    }
    fn take_back(&mut self, n: usize) -> Option<&'b [u8]> {
        if <[u8]>::len(self) < n {
            return None;
        }
        Some(take_back_mut(self, n))
    }
    fn into_rest(self) -> &'b [u8] {
        self
    }
}

impl<'b, 'a: 'b> BufferView<&'b mut [u8]> for &'b mut &'a mut [u8] {
    fn len(&self) -> usize {
        <[u8]>::len(self)
    }
    fn take_front(&mut self, n: usize) -> Option<&'b mut [u8]> {
        if <[u8]>::len(self) < n {
            return None;
        }
        Some(take_front_mut(self, n))
    }
    fn take_back(&mut self, n: usize) -> Option<&'b mut [u8]> {
        if <[u8]>::len(self) < n {
            return None;
        }
        Some(take_back_mut(self, n))
    }
    fn into_rest(self) -> &'b mut [u8] {
        self
    }
}

impl<'b, 'a: 'b> BufferViewMut<&'b mut [u8]> for &'b mut &'a mut [u8] {}

fn take_front<'a>(bytes: &mut &'a [u8], n: usize) -> &'a [u8] {
    let (prefix, rest) = mem::replace(bytes, &[]).split_at(n);
    mem::replace(bytes, rest);
    prefix
}

fn take_back<'a>(bytes: &mut &'a [u8], n: usize) -> &'a [u8] {
    let split = bytes.len() - n;
    let (rest, suffix) = mem::replace(bytes, &[]).split_at(split);
    mem::replace(bytes, rest);
    suffix
}

fn take_front_mut<'a>(bytes: &mut &'a mut [u8], n: usize) -> &'a mut [u8] {
    let (prefix, rest) = mem::replace(bytes, &mut []).split_at_mut(n);
    mem::replace(bytes, rest);
    prefix
}

fn take_back_mut<'a>(bytes: &mut &'a mut [u8], n: usize) -> &'a mut [u8] {
    let split = <[u8]>::len(bytes) - n;
    let (rest, suffix) = mem::replace(bytes, &mut []).split_at_mut(split);
    mem::replace(bytes, rest);
    suffix
}

// return the inclusive-exclusive equivalent of the bound, verifying that it
// is in range of len, and panicking if it is not or if the range is
// nonsensical
fn canonicalize_range<R: RangeBounds<usize>>(len: usize, range: &R) -> Range<usize> {
    let lower = canonicalize_lower_bound(range.start_bound());
    let upper = canonicalize_upper_bound(len, range.end_bound()).expect("range out of bounds");
    assert!(lower <= upper, "invalid range: upper bound precedes lower bound");
    lower..upper
}

// return the inclusive equivalent of the bound
fn canonicalize_lower_bound(bound: Bound<&usize>) -> usize {
    match bound {
        Bound::Included(x) => *x,
        Bound::Excluded(x) => *x + 1,
        Bound::Unbounded => 0,
    }
}

// return the exclusive equivalent of the bound, verifying that it is in
// range of len
fn canonicalize_upper_bound(len: usize, bound: Bound<&usize>) -> Option<usize> {
    let bound = match bound {
        Bound::Included(x) => *x + 1,
        Bound::Excluded(x) => *x,
        Bound::Unbounded => len,
    };
    if bound > len {
        return None;
    }
    Some(bound)
}

#[cfg(test)]
mod tests {
    use super::*;

    trait BufferExt: BufferMut {
        fn parts_mut(&mut self) -> (&mut [u8], &mut [u8], &mut [u8]);
    }

    impl<B: BufferMut> BufferExt for B {
        fn parts_mut(&mut self) -> (&mut [u8], &mut [u8], &mut [u8]) {
            let prefix = self.prefix_len();
            let body = self.len();

            self.reset();
            let (prefix, rest) = self.as_mut().split_at_mut(prefix);
            let (body, suffix) = rest.split_at_mut(body);
            (prefix, body, suffix)
        }
    }

    // Call test_buffer, test_buffer_view, and test_buffer_view_post for each of
    // the Buffer types. Call test_parse_buffer and test_buffer_view for each of
    // the ParseBuffer types.

    #[test]
    fn test_byte_slice_impl_buffer() {
        // Box::leak allows us to create a static slice reference, which is the
        // only way we can return a slice reference from a function like this.
        test_parse_buffer::<&[u8], _>(|len| Box::leak(Box::new(ascending(len))));
        let buf = ascending(10);
        let mut buf: &[u8] = buf.as_ref();
        test_buffer_view::<&[u8], _>(&mut buf);
    }

    #[test]
    fn test_byte_slice_mut_impl_buffer() {
        // Box::leak allows us to create a static slice reference, which is the
        // only way we can return a slice reference from a function like this.
        test_parse_buffer::<&mut [u8], _>(|len| Box::<Vec<u8>>::leak(Box::new(ascending(len))));
        let mut buf = ascending(10);
        let mut buf: &mut [u8] = buf.as_mut();
        test_buffer_view::<&mut [u8], _>(&mut buf);
    }

    #[test]
    fn test_either_impl_buffer() {
        macro_rules! test_either {
            ($variant:ident) => {
                test_buffer::<Either<Buf<Vec<u8>>, Buf<Vec<u8>>>, _>(|len| {
                    Either::$variant(Buf::new(ascending(len), ..))
                });
                // Test call to `Buf::buffer_view` which returns a
                // `BufferView`.
                let mut buf: Either<Buf<Vec<u8>>, Buf<Vec<u8>>> =
                    Either::$variant(Buf::new(ascending(10), ..));
                test_buffer_view(match &mut buf {
                    Either::$variant(buf) => buf.buffer_view(),
                    _ => unreachable!(),
                });
                test_buffer_view_post(&buf, true);
                // Test call to `Buf::buffer_view_mut` which returns a
                // `BufferViewMut`.
                let mut buf: Either<Buf<Vec<u8>>, Buf<Vec<u8>>> =
                    Either::$variant(Buf::new(ascending(10), ..));
                test_buffer_view_mut(match &mut buf {
                    Either::$variant(buf) => buf.buffer_view_mut(),
                    _ => unreachable!(),
                });
                test_buffer_view_mut_post(&buf, true);
            };
        }

        test_either!(A);
        test_either!(B);
    }

    #[test]
    fn test_buf_impl_buffer() {
        test_buffer(|len| Buf::new(ascending(len), ..));
        let mut buf = Buf::new(ascending(10), ..);
        test_buffer_view(buf.buffer_view());
        test_buffer_view_post(&buf, true);
    }

    fn ascending(n: u8) -> Vec<u8> {
        (0..n).collect::<Vec<u8>>()
    }

    // This test performs a number of shrinking operations (for ParseBuffer
    // implementations) followed by their equivalent growing operations (for
    // Buffer implementations only), and at each step, verifies various
    // properties of the buffer. The shrinking part of the test is in
    // test_parse_buffer_inner, while test_buffer calls test_parse_buffer_inner
    // and then performs the growing part of the test.

    // When shrinking, we keep two buffers - 'at_once' and 'separately', and for
    // each test case, we do the following:
    // - shrink the 'at_once' buffer with the 'shrink' field
    // - shrink_front the 'separately' buffer with the 'front' field
    // - shrink_back the 'separately' buffer with the 'back' field
    //
    // When growing, we only keep one buffer from the shrinking phase, and for
    // each test case, we do the following:
    // - grow_front the buffer with the 'front' field
    // - grow_back the buffer with the 'back' field
    //
    // After each action, we verify that the len and contents are as expected.
    // For Buffers, we also verify the cap, prefix, and suffix.
    struct TestCase {
        shrink: Range<usize>,
        front: usize, // shrink or grow the front of the body
        back: usize,  // shrink or grow the back of the body
        cap: usize,
        len: usize,
        pfx: usize,
        sfx: usize,
        contents: &'static [u8],
    }
    #[rustfmt::skip]
    const TEST_CASES: &[TestCase] = &[
        TestCase { shrink: 0..10, front: 0, back: 0, cap: 10, len: 10, pfx: 0, sfx: 0, contents: &[0, 1, 2, 3, 4, 5, 6, 7, 8, 9], },
        TestCase { shrink: 2..10, front: 2, back: 0, cap: 10, len: 8,  pfx: 2, sfx: 0, contents: &[2, 3, 4, 5, 6, 7, 8, 9], },
        TestCase { shrink: 0..8,  front: 0, back: 0, cap: 10, len: 8,  pfx: 2, sfx: 0, contents: &[2, 3, 4, 5, 6, 7, 8, 9], },
        TestCase { shrink: 0..6,  front: 0, back: 2, cap: 10, len: 6,  pfx: 2, sfx: 2, contents: &[2, 3, 4, 5, 6, 7], },
        TestCase { shrink: 2..4,  front: 2, back: 2, cap: 10, len: 2,  pfx: 4, sfx: 4, contents: &[4, 5], },
    ];

    // Test a ParseBuffer implementation. 'new_buf' is a function which
    // constructs a buffer of length n, and initializes its contents to [0, 1,
    // 2, ..., n -1].
    fn test_parse_buffer<B: ParseBuffer, N: Fn(u8) -> B>(new_buf: N) {
        test_parse_buffer_inner(new_buf, |buf, _, len, _, _, contents| {
            assert_eq!(buf.len(), len);
            assert_eq!(buf.as_ref(), contents);
        });
    }

    // Code common to test_parse_buffer and test_buffer. 'assert' is a function
    // which takes a buffer, and verifies that its capacity, length, prefix,
    // suffix, and contents are equal to the arguments (in that order). For
    // ParseBuffers, the capacity, prefix, and suffix arguments are irrelevant,
    // and ignored.
    //
    // When the test is done, test_parse_buffer_inner returns one of the buffers
    // it used for testing so that test_buffer can do further testing on it. Its
    // prefix, body, and suffix will be [0, 1, 2, 3], [4, 5], and [6, 7, 8, 9]
    // respectively.
    fn test_parse_buffer_inner<
        B: ParseBuffer,
        N: Fn(u8) -> B,
        A: Fn(&B, usize, usize, usize, usize, &[u8]),
    >(
        new_buf: N,
        assert: A,
    ) -> B {
        let mut at_once = new_buf(10);
        let mut separately = new_buf(10);
        for tc in TEST_CASES {
            at_once.shrink(tc.shrink.clone());
            separately.shrink_front(tc.front);
            separately.shrink_back(tc.back);
            assert(&at_once, tc.cap, tc.len, tc.pfx, tc.sfx, tc.contents);
            assert(&separately, tc.cap, tc.len, tc.pfx, tc.sfx, tc.contents);
        }
        at_once
    }

    // Test a Buffer implementation. 'new_buf' is a function which constructs a
    // buffer of length and capacity n, and initializes its contents to [0, 1,
    // 2, ..., n - 1].
    fn test_buffer<B: Buffer, F: Fn(u8) -> B>(new_buf: F) {
        fn assert<B: Buffer>(
            buf: &B,
            cap: usize,
            len: usize,
            pfx: usize,
            sfx: usize,
            contents: &[u8],
        ) {
            assert_eq!(buf.len(), len);
            assert_eq!(buf.capacity(), cap);
            assert_eq!(buf.prefix_len(), pfx);
            assert_eq!(buf.suffix_len(), sfx);
            assert_eq!(buf.as_ref(), contents);
        }

        let mut buf = test_parse_buffer_inner(new_buf, assert);
        buf.reset();
        assert(&buf, 10, 10, 0, 0, &[0, 1, 2, 3, 4, 5, 6, 7, 8, 9][..]);
        buf.shrink_front(4);
        buf.shrink_back(4);
        assert(&buf, 10, 2, 4, 4, &[4, 5][..]);

        for tc in TEST_CASES.iter().rev() {
            assert(&buf, tc.cap, tc.len, tc.pfx, tc.sfx, tc.contents);
            buf.grow_front(tc.front);
            buf.grow_back(tc.back);
        }
    }

    // Test a BufferView implementation. Call with a view into a buffer with no
    // extra capacity whose body contains [0, 1, ..., 9]. After the call
    // returns, call test_buffer_view_post on the buffer.
    fn test_buffer_view<B: ByteSlice, BV: BufferView<B>>(mut view: BV) {
        assert_eq!(view.len(), 10);
        assert_eq!(view.take_front(1).unwrap().as_ref(), &[0][..]);
        assert_eq!(view.len(), 9);
        assert_eq!(view.take_back(1).unwrap().as_ref(), &[9][..]);
        assert_eq!(view.len(), 8);
        assert_eq!(view.take_obj_front::<[u8; 2]>().unwrap().as_ref(), [1, 2]);
        assert_eq!(view.len(), 6);
        assert_eq!(view.take_obj_back::<[u8; 2]>().unwrap().as_ref(), [7, 8]);
        assert_eq!(view.len(), 4);
        assert!(view.take_front(5).is_none());
        assert_eq!(view.len(), 4);
        assert!(view.take_back(5).is_none());
        assert_eq!(view.len(), 4);
        assert_eq!(view.into_rest().as_ref(), &[3, 4, 5, 6][..]);
    }

    // Test a BufferViewMut implementation. Call with a mutable view into a buffer
    // with no extra capacity whose body contains [0, 1, ..., 9]. After the call
    // returns, call test_buffer_view_post on the buffer.
    fn test_buffer_view_mut<B: ByteSliceMut, BV: BufferViewMut<B>>(mut view: BV) {
        assert_eq!(view.len(), 10);
        assert_eq!(view.as_mut()[0], 0);
        assert_eq!(view.take_front_zero(1).unwrap().as_ref(), &[0][..]);
        assert_eq!(view.len(), 9);
        assert_eq!(view.as_mut()[0], 1);
        assert_eq!(view.take_front_zero(1).unwrap().as_ref(), &[0][..]);
        assert_eq!(view.len(), 8);
        assert_eq!(view.as_mut()[7], 9);
        assert_eq!(view.take_back_zero(1).unwrap().as_ref(), &[0][..]);
        assert_eq!(view.len(), 7);
        assert_eq!(&view.as_mut()[0..2], &[2, 3][..]);
        assert_eq!(view.take_obj_front_zero::<[u8; 2]>().unwrap().as_ref(), &[0, 0][..]);
        assert_eq!(view.len(), 5);
        assert_eq!(&view.as_mut()[3..5], &[7, 8][..]);
        assert_eq!(view.take_obj_back_zero::<[u8; 2]>().unwrap().as_ref(), &[0, 0][..]);
        assert_eq!(view.len(), 3);
        assert!(view.take_front_zero(5).is_none());
        assert_eq!(view.len(), 3);
        assert!(view.take_back_zero(5).is_none());
        assert_eq!(view.len(), 3);
        assert_eq!(view.as_mut(), &[4, 5, 6][..]);
        assert_eq!(view.into_rest_zero().as_ref(), &[0, 0, 0][..]);
    }

    // Post-verification to test a BufferView implementation. Call after
    // test_buffer_view.
    fn test_buffer_view_post<B: Buffer>(buffer: &B, preserves_cap: bool) {
        assert_eq!(buffer.as_ref(), &[3, 4, 5, 6][..]);
        if preserves_cap {
            assert_eq!(buffer.prefix_len(), 3);
            assert_eq!(buffer.suffix_len(), 3);
        }
    }

    // Post-verification to test a BufferViewMut implementation. Call after
    // test_buffer_view_mut.
    fn test_buffer_view_mut_post<B: Buffer>(buffer: &B, preserves_cap: bool) {
        assert_eq!(buffer.as_ref(), &[0, 0, 0][..]);
        if preserves_cap {
            assert_eq!(buffer.prefix_len(), 4);
            assert_eq!(buffer.suffix_len(), 3);
        }
    }

    #[test]
    fn test_buffer_view_from_buffer() {
        // This test is specifically designed to verify that implementations of
        // ParseBuffer::parse properly construct a BufferView, and that that
        // BufferView properly updates the underlying buffer. It was inspired by
        // the bug with Change-Id Ifeab21fba0f7ba94d1a12756d4e83782002e4e1e.

        // This ParsablePacket implementation takes the contents it expects as a
        // parse argument and validates the BufferView[Mut] against it. It consumes
        // one byte from the front and one byte from the back to ensure that that
        // functionality works as well. For a mutable buffer, the implementation also
        // modifies the bytes that were consumed so tests can make sure that the
        // `parse_mut` function was actually called and that the bytes are mutable.
        impl<B: ByteSlice> ParsablePacket<B, &[u8]> for () {
            type Error = ();
            fn parse<BV: BufferView<B>>(mut buffer: BV, args: &[u8]) -> Result<(), ()> {
                assert_eq!(buffer.as_ref(), args);
                buffer.take_front(1);
                buffer.take_back(1);
                Ok(())
            }

            fn parse_mut<BV: BufferViewMut<B>>(mut buffer: BV, args: &[u8]) -> Result<(), ()>
            where
                B: ByteSliceMut,
            {
                assert_eq!(buffer.as_ref(), args);
                buffer.take_front(1).unwrap().as_mut()[0] += 1;
                buffer.take_back(1).unwrap().as_mut()[0] += 2;
                Ok(())
            }

            fn parse_metadata(&self) -> ParseMetadata {
                unimplemented!()
            }
        }

        // immutable byte slices

        let mut buf = &[0, 1, 2, 3, 4, 5, 6, 7][..];
        buf.parse_with::<_, ()>(&[0, 1, 2, 3, 4, 5, 6, 7]).unwrap();
        // test that, after parsing, the bytes consumed are consumed permanently
        buf.parse_with::<_, ()>(&[1, 2, 3, 4, 5, 6]).unwrap();

        // test that different temporary values do not affect one another and
        // also that slicing works properly (in that the elements outside of the
        // slice are not exposed in the BufferView[Mut]; this is fairly obvious
        // for slices, but less obvious for Buf, which we test below)
        let buf = &[0, 1, 2, 3, 4, 5, 6, 7][..];
        (&buf[1..7]).parse_with::<_, ()>(&[1, 2, 3, 4, 5, 6]).unwrap();
        (&buf[1..7]).parse_with::<_, ()>(&[1, 2, 3, 4, 5, 6]).unwrap();

        // mutable byte slices

        let mut bytes = [0, 1, 2, 3, 4, 5, 6, 7];
        let mut buf = &mut bytes[..];
        buf.parse_with::<_, ()>(&[0, 1, 2, 3, 4, 5, 6, 7]).unwrap();
        // test that, after parsing, the bytes consumed are consumed permanently
        buf.parse_with::<_, ()>(&[1, 2, 3, 4, 5, 6]).unwrap();
        // test that this also works with parse_with_mut
        buf.parse_with_mut::<_, ()>(&[2, 3, 4, 5]).unwrap();
        buf.parse_with_mut::<_, ()>(&[3, 4]).unwrap();
        assert_eq!(bytes, [0, 1, 3, 4, 6, 7, 6, 7]);

        // test that different temporary values do not affect one another and
        // also that slicing works properly (in that the elements outside of the
        // slice are not exposed in the BufferView[Mut]; this is fairly obvious
        // for slices, but less obvious for Buf, which we test below)
        let buf = &mut [0, 1, 2, 3, 4, 5, 6, 7][..];
        (&buf[1..7]).parse_with::<_, ()>(&[1, 2, 3, 4, 5, 6]).unwrap();
        (&buf[1..7]).parse_with::<_, ()>(&[1, 2, 3, 4, 5, 6]).unwrap();
        (&mut buf[1..7]).parse_with_mut::<_, ()>(&[1, 2, 3, 4, 5, 6]).unwrap();
        (&mut buf[1..7]).parse_with_mut::<_, ()>(&[2, 2, 3, 4, 5, 8]).unwrap();
        assert_eq!(buf, &[0, 3, 2, 3, 4, 5, 10, 7][..]);

        // Buf with immutable byte slice

        let mut buf = Buf::new(&[0, 1, 2, 3, 4, 5, 6, 7][..], ..);
        buf.parse_with::<_, ()>(&[0, 1, 2, 3, 4, 5, 6, 7]).unwrap();
        // test that, after parsing, the bytes consumed are consumed permanently
        buf.parse_with::<_, ()>(&[1, 2, 3, 4, 5, 6]).unwrap();

        // the same test again, but this time with Buf's range set
        let mut buf = Buf::new(&[0, 1, 2, 3, 4, 5, 6, 7][..], 1..7);
        buf.parse_with::<_, ()>(&[1, 2, 3, 4, 5, 6]).unwrap();
        // test that, after parsing, the bytes consumed are consumed permanently
        buf.parse_with::<_, ()>(&[2, 3, 4, 5]).unwrap();

        // Buf with mutable byte slice

        let mut bytes = [0, 1, 2, 3, 4, 5, 6, 7];
        let buf = &mut bytes[..];
        let mut buf = Buf::new(&mut buf[..], ..);
        buf.parse_with::<_, ()>(&[0, 1, 2, 3, 4, 5, 6, 7]).unwrap();
        // test that, after parsing, the bytes consumed are consumed permanently
        buf.parse_with::<_, ()>(&[1, 2, 3, 4, 5, 6]).unwrap();
        // test that this also works with parse_with_mut
        buf.parse_with_mut::<_, ()>(&[2, 3, 4, 5]).unwrap();
        buf.parse_with_mut::<_, ()>(&[3, 4]).unwrap();
        assert_eq!(bytes, [0, 1, 3, 4, 6, 7, 6, 7]);
        // the same test again, but this time with Buf's range set
        let mut bytes = [0, 1, 2, 3, 4, 5, 6, 7];
        let buf = &mut bytes[..];
        let mut buf = Buf::new(&mut buf[..], 1..7);
        buf.parse_with::<_, ()>(&[1, 2, 3, 4, 5, 6]).unwrap();
        // test that, after parsing, the bytes consumed are consumed permanently
        buf.parse_with::<_, ()>(&[2, 3, 4, 5]).unwrap();
        assert_eq!(bytes, [0, 1, 2, 3, 4, 5, 6, 7]);
        // test that this also works with parse_with_mut
        let mut bytes = [0, 1, 2, 3, 4, 5, 6, 7];
        let buf = &mut bytes[..];
        let mut buf = Buf::new(&mut buf[..], 1..7);
        buf.parse_with_mut::<_, ()>(&[1, 2, 3, 4, 5, 6]).unwrap();
        buf.parse_with_mut::<_, ()>(&[2, 3, 4, 5]).unwrap();
        assert_eq!(bytes, [0, 2, 3, 3, 4, 7, 8, 7]);
    }

    #[test]
    fn test_buf_shrink_to() {
        // Tests the shrink_front_to and shrink_back_to methods.
        fn test(buf: &[u8], shrink_to: usize, size_after: usize) {
            let mut buf0 = &buf[..];
            buf0.shrink_front_to(shrink_to);
            assert_eq!(buf0.len(), size_after);
            let mut buf1 = &buf[..];
            buf1.shrink_back_to(shrink_to);
            assert_eq!(buf0.len(), size_after);
        }

        test(&[0, 1, 2, 3], 2, 2);
        test(&[0, 1, 2, 3], 4, 4);
        test(&[0, 1, 2, 3], 8, 4);
    }

    // Each panic test case needs to be in its own function, which results in an
    // explosion of test functions. These macros generates the appropriate
    // function definitions automatically for a given type, reducing the amount
    // of code by a factor of ~4.
    macro_rules! make_parse_buffer_panic_tests {
        (
            $new_empty_buffer:expr,
            $shrink_panics:ident,
            $nonsense_shrink_panics:ident,
        ) => {
            #[test]
            #[should_panic]
            fn $shrink_panics() {
                ($new_empty_buffer).shrink(..1);
            }
            #[test]
            #[should_panic]
            fn $nonsense_shrink_panics() {
                ($new_empty_buffer).shrink(1..0);
            }
        };
    }

    macro_rules! make_panic_tests {
        (
            $new_empty_buffer:expr,
            $shrink_panics:ident,
            $nonsense_shrink_panics:ident,
            $grow_front_panics:ident,
            $grow_back_panics:ident,
        ) => {
            make_parse_buffer_panic_tests!(
                $new_empty_buffer,
                $shrink_panics,
                $nonsense_shrink_panics,
            );
            #[test]
            #[should_panic]
            fn $grow_front_panics() {
                ($new_empty_buffer).grow_front(1);
            }
            #[test]
            #[should_panic]
            fn $grow_back_panics() {
                ($new_empty_buffer).grow_back(1);
            }
        };
    }

    make_parse_buffer_panic_tests!(
        &[][..],
        test_byte_slice_shrink_panics,
        test_byte_slice_nonsense_shrink_panics,
    );
    make_parse_buffer_panic_tests!(
        &mut [][..],
        test_byte_slice_mut_shrink_panics,
        test_byte_slice_mut_nonsense_shrink_panics,
    );
    make_panic_tests!(
        Either::A::<Buf<&[u8]>, Buf<&[u8]>>(Buf::new(&[][..], ..)),
        test_either_slice_panics,
        test_either_nonsense_slice_panics,
        test_either_grow_front_panics,
        test_either_grow_back_panics,
    );
    make_panic_tests!(
        Buf::new(&[][..], ..),
        test_buf_shrink_panics,
        test_buf_nonsense_shrink_panics,
        test_buf_grow_front_panics,
        test_buf_grow_back_panics,
    );
}
