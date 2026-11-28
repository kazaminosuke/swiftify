// SPDX-License-Identifier: GPL-3.0-or-later

use std::{env, error::Error, io, thread, time::Duration};
use swiftify_native_audio::{AudioError, Player, State};

fn main() -> Result<(), Box<dyn Error>> {
    let path = env::args_os().nth(1).ok_or_else(|| {
        io::Error::new(
            io::ErrorKind::InvalidInput,
            "usage: cargo run --example play-file -- <audio-file>",
        )
    })?;

    let mut player = Player::open()?;
    player.load_file(path)?;
    player.play()?;
    loop {
        let snapshot = player.snapshot();
        match snapshot.state {
            State::Ended => {
                player.stop()?;
                println!(
                    "Submitted {} ms of audio.",
                    snapshot.submitted_position.as_millis()
                );
                return Ok(());
            }
            State::SourceFailed => {
                return Err(AudioError::InputRead(
                    snapshot.input_error.unwrap_or(io::ErrorKind::Other),
                )
                .into());
            }
            State::OutputLost => return Err(AudioError::OutputLost.into()),
            _ => thread::sleep(Duration::from_millis(25)),
        }
    }
}
