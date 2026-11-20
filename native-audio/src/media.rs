// SPDX-License-Identifier: GPL-3.0-or-later

use std::{
    fs::{self, File},
    io::{self, Read, Seek, SeekFrom},
    path::Path,
    sync::{Arc, Mutex},
};

/// Opens independent readers for the same recording. The player can keep the
/// previous reader active while preparing a new one for a seek.
pub trait MediaProvider: Send + Sync {
    fn open(&self) -> io::Result<MediaInput>;
}

trait ReadSeek: Read + Seek + Send + Sync {}
impl<T: Read + Seek + Send + Sync> ReadSeek for T {}

/// A seekable stream starting at its first byte. Length and extension are
/// optional. Remote implementations must bound read and seek times.
pub struct MediaInput {
    reader: Box<dyn ReadSeek>,
    pub(crate) byte_len: Option<u64>,
    pub(crate) extension: Option<String>,
    pub(crate) failure: ReadFailure,
}

impl MediaInput {
    pub fn new(
        reader: impl Read + Seek + Send + Sync + 'static,
        byte_len: Option<u64>,
        extension: Option<String>,
    ) -> Self {
        Self {
            reader: Box::new(reader),
            byte_len,
            extension,
            failure: ReadFailure::default(),
        }
    }
}

impl Read for MediaInput {
    fn read(&mut self, buffer: &mut [u8]) -> io::Result<usize> {
        self.reader.read(buffer).inspect_err(|error| {
            if error.kind() != io::ErrorKind::Interrupted {
                self.failure.record(error.kind());
            }
        })
    }
}

impl Seek for MediaInput {
    fn seek(&mut self, position: SeekFrom) -> io::Result<u64> {
        self.reader.seek(position)
    }
}

impl symphonia::core::io::MediaSource for MediaInput {
    fn is_seekable(&self) -> bool {
        true
    }
    fn byte_len(&self) -> Option<u64> {
        self.byte_len
    }
}

/// Keeps the first terminal failure without copying credential-bearing details.
#[derive(Clone, Default)]
pub(crate) struct ReadFailure(Arc<Mutex<Option<io::ErrorKind>>>);

impl ReadFailure {
    pub(crate) fn record(&self, kind: io::ErrorKind) {
        if let Ok(mut failure) = self.0.lock() {
            failure.get_or_insert(kind);
        }
    }

    #[cfg(feature = "player")]
    pub(crate) fn kind(&self) -> Option<io::ErrorKind> {
        self.0.lock().ok().and_then(|failure| *failure)
    }
}

#[cfg(feature = "player")]
pub(crate) struct FileMedia(pub(crate) std::path::PathBuf);

#[cfg(feature = "player")]
impl MediaProvider for FileMedia {
    fn open(&self) -> io::Result<MediaInput> {
        open_regular_file(&self.0)
    }
}

pub(crate) fn open_regular_file(path: &Path) -> io::Result<MediaInput> {
    if !fs::metadata(path)?.is_file() {
        return Err(io::Error::new(
            io::ErrorKind::InvalidInput,
            "expected a regular file",
        ));
    }
    let file = File::open(path)?;
    let length = file.metadata()?.len();
    let extension = path
        .extension()
        .and_then(|ext| ext.to_str())
        .map(str::to_owned);
    Ok(MediaInput::new(file, Some(length), extension))
}
