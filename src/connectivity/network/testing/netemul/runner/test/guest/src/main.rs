// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await)]

use {
    failure::{Error, ResultExt},
    fidl::endpoints::ClientEnd,
    fidl_fuchsia_io::FileMarker,
    fidl_fuchsia_netemul_guest::{
        CommandListenerEvent, CommandListenerEventStream, CommandListenerMarker,
        EnvironmentVariable, GuestDiscoveryMarker, GuestInteractionMarker,
    },
    fuchsia_async as fasync,
    fuchsia_component::client,
    fuchsia_zircon as zx,
    futures::TryStreamExt,
    rand::distributions::Alphanumeric,
    rand::{thread_rng, Rng},
    std::fs::File,
    std::io::prelude::*,
    std::io::Read,
};

fn create_test_data(file_path: String, file_size: usize) -> Result<(), Error> {
    let file_contents: String = thread_rng().sample_iter(&Alphanumeric).take(file_size).collect();
    let mut test_file = File::create(file_path)?;
    test_file.write_all(file_contents.as_bytes())?;
    return Ok(());
}

fn file_to_client(file: &File) -> Result<ClientEnd<FileMarker>, Error> {
    let channel = fdio::clone_channel(file)?;
    return Ok(ClientEnd::new(channel));
}

async fn test_file_transfer() -> Result<(), Error> {
    let local_test_data = "/data/test_data.txt";
    let guest_destination = "/root/input/test_data.txt";
    let verification_copy = "/data/test_data_copy.txt";

    let guest_discovery_service = client::connect_to_service::<GuestDiscoveryMarker>()?;
    let (gis, gis_ch) = fidl::endpoints::create_proxy::<GuestInteractionMarker>()?;
    let () = guest_discovery_service.get_guest(None, "debian_guest", gis_ch)?;

    create_test_data(local_test_data.to_string(), 4096)?;

    // Push the file to the guest
    let put_file = file_to_client(&File::open(local_test_data)?)?;
    let put_status =
        gis.put_file(put_file, guest_destination).await.context("Failed to put file")?;
    zx::ok(put_status)?;

    // Retrieve the file from the guest
    let get_file = file_to_client(&File::create(verification_copy)?)?;
    let get_status =
        gis.get_file(guest_destination, get_file).await.context("Failed to get file")?;
    zx::ok(get_status)?;

    // Compare the original file to the one copied back from the guest.
    let mut original_contents = String::new();
    let mut copy_contents = String::new();

    let mut original_file = File::open(local_test_data)?;
    original_file.read_to_string(&mut original_contents)?;

    let mut verification_file = File::open(verification_copy)?;
    verification_file.read_to_string(&mut copy_contents)?;

    assert_eq!(original_contents, copy_contents);

    return Ok(());
}

async fn wait_for_command_completion(
    mut stream: CommandListenerEventStream,
    stdin_socket: zx::Socket,
    to_write: String,
) -> Result<(), Error> {
    loop {
        let event = stream.try_next().await?;
        assert!(event.is_some());
        match event.unwrap() {
            CommandListenerEvent::OnStarted { status } => {
                zx::ok(status)?;
                stdin_socket.write(to_write.as_bytes())?;
            }
            CommandListenerEvent::OnTerminated { status, return_code } => {
                zx::ok(status)?;
                assert_eq!(return_code, 0);
                return Ok(());
            }
        }
    }
}

async fn test_exec_script() -> Result<(), Error> {
    // Command to run, environment variable definitions, and stdin to input.
    let command_to_run = "/bin/sh -c \"/root/input/test_script.sh\"";
    let stdin_input = "hello\n";
    let stdout_env_var = "STDOUT_STRING";
    let stderr_env_var = "STDERR_STRING";
    let stdout_expected_string = "stdout";
    let stderr_expected_string = "stderr";
    let mut env = vec![
        EnvironmentVariable {
            key: stdout_env_var.to_string(),
            value: stdout_expected_string.to_string(),
        },
        EnvironmentVariable {
            key: stderr_env_var.to_string(),
            value: stderr_expected_string.to_string(),
        },
    ];

    // Request that the guest interaction service run the command.
    let guest_discovery_service = client::connect_to_service::<GuestDiscoveryMarker>()?;
    let (gis, gis_ch) = fidl::endpoints::create_proxy::<GuestInteractionMarker>()?;
    let () = guest_discovery_service.get_guest(None, "debian_guest", gis_ch)?;

    let (stdin_0, stdin_1) = zx::Socket::create(zx::SocketOpts::STREAM).unwrap();
    let (stdout_0, stdout_1) = zx::Socket::create(zx::SocketOpts::STREAM).unwrap();
    let (stderr_0, stderr_1) = zx::Socket::create(zx::SocketOpts::STREAM).unwrap();

    let (client_proxy, server_end) = fidl::endpoints::create_proxy::<CommandListenerMarker>()
        .context("Failed to create CommandListener ends")?;

    gis.execute_command(
        command_to_run,
        &mut env.iter_mut(),
        Some(stdin_1),
        Some(stdout_1),
        Some(stderr_1),
        server_end,
    )?;

    // Ensure that the process completes normally.
    wait_for_command_completion(client_proxy.take_event_stream(), stdin_0, stdin_input.to_string())
        .await?;

    // Validate the stdout and stderr.
    let mut guest_stdout = vec![0; 6];
    let mut guest_stderr = vec![0; 6];
    stdout_0.read(&mut guest_stdout)?;
    stderr_0.read(&mut guest_stderr)?;
    assert_eq!(std::str::from_utf8(&guest_stdout)?, stdout_expected_string.to_string());
    assert_eq!(std::str::from_utf8(&guest_stderr)?, stderr_expected_string.to_string());

    // Pull the file that was created by the script and validate its contents.
    let local_copy = "/data/script_output_copy.txt";
    let outfile_location = "/root/output/script_output.txt";

    let get_file = file_to_client(&File::create(local_copy)?)?;
    let get_status = gis
        .get_file(outfile_location, get_file)
        .await
        .context("Failed waiting for file transfer.")?;
    zx::ok(get_status).context("Failed to get requested file.")?;

    let mut file_contents = String::new();
    let mut stdin_file = File::open(local_copy)?;
    stdin_file.read_to_string(&mut file_contents)?;

    assert_eq!(file_contents, stdin_input.to_string());

    return Ok(());
}

async fn do_run() -> Result<(), Error> {
    test_file_transfer().await?;
    test_exec_script().await?;
    return Ok(());
}

fn main() -> Result<(), Error> {
    let mut executor = fasync::Executor::new().context("Error creating executor")?;
    executor.run_singlethreaded(do_run())?;
    return Ok(());
}