// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    rust_icu_common as common, rust_icu_sys as sys, rust_icu_sys::versioned_function,
    rust_icu_sys::*, std::convert::TryFrom, std::os::raw,
};

/// Implements `UDataMemory`.
///
/// Represents data memory backed by a borrowed memory buffer.
pub struct UDataMemory<'a> {
    // The buffer backing this data memory.  We're holding a reference here
    // so that we're sure the underlying buffer won't go away.
    #[allow(dead_code)]
    buf: &'a [u8],
}

impl<'a> TryFrom<&'a Vec<u8>> for crate::UDataMemory<'a> {
    type Error = common::Error;
    /// Makes a UDataMemory out of a buffer.
    ///
    /// Implements `udata_setCommonData`.
    fn try_from(buf: &'a Vec<u8>) -> Result<Self, Self::Error> {
        let mut status = sys::UErrorCode::U_ZERO_ERROR;
        // Expects that buf is a valid pointer and that it contains valid
        // ICU data.  If data is invalid, an error status will be set.
        // No guarantees for invalid pointers.
        unsafe {
            versioned_function!(udata_setCommonData)(
                buf.as_ptr() as *const raw::c_void,
                &mut status,
            );
        };
        common::Error::ok_or_warning(status)?;
        Ok(UDataMemory { buf: buf.as_ref() })
    }
}