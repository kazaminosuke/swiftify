// SPDX-License-Identifier: GPL-3.0-or-later

//! Provider-independent, native audio playback for Swiftify.

#[doc(hidden)]
pub mod device;

#[cfg(feature = "file-codecs")]
pub mod decode;

#[cfg(feature = "file-codecs")]
pub mod media;

#[cfg(feature = "player")]
mod player;

#[cfg(feature = "player")]
pub use device::{OutputDevice, output_devices};

#[cfg(feature = "player")]
pub use player::{AudioError, Player, Snapshot, State};
