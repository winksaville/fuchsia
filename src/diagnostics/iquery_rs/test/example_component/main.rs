// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{deprecated_fidl_server::*, table::*},
    failure::Error,
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_inspect::*,
    futures::StreamExt,
    std::ops::AddAssign,
    structopt::StructOpt,
};

mod deprecated_fidl_server;
mod table;

struct PopulateParams<T> {
    floor: T,
    step: T,
    count: usize,
}

fn populated<H: HistogramProperty>(histogram: H, params: PopulateParams<H::Type>) -> H
where
    H::Type: AddAssign + Copy,
{
    let mut value = params.floor;
    for _ in 0..params.count {
        histogram.insert(value);
        value += params.step;
    }
    histogram
}

#[derive(Debug, StructOpt)]
#[structopt(
    name = "example",
    about = "Example component to showcase Inspect API objects, including an NxM nested table"
)]
struct Options {
    #[structopt(long)]
    rows: usize,

    #[structopt(long)]
    columns: usize,
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let opts = Options::from_args();
    if opts.rows == 0 || opts.columns == 0 {
        Options::clap().print_help()?;
        std::process::exit(1);
    }

    let inspector = Inspector::new();
    let root = inspector.root();
    assert!(inspector.is_valid());

    reset_unique_names();
    let table_node_name = unique_name("table");
    let example_table =
        Table::new(opts.rows, opts.columns, &table_node_name, root.create_child(&table_node_name));

    let _int_array = root.create_int_array(unique_name("array"), 3);
    _int_array.set(0, 1);
    _int_array.add(1, 10);
    _int_array.subtract(2, 3);

    let _uint_array = root.create_uint_array(unique_name("array"), 3);
    _uint_array.set(0, 1);
    _uint_array.add(1, 10);
    _uint_array.set(2, 3);
    _uint_array.subtract(2, 1);

    let _double_array = root.create_double_array(unique_name("array"), 3);
    _double_array.set(0, 0.25);
    _double_array.add(1, 1.25);
    _double_array.subtract(2, 0.75);

    let _int_linear_hist = populated(
        root.create_int_linear_histogram(
            unique_name("histogram"),
            LinearHistogramParams { floor: -10, step_size: 5, buckets: 3 },
        ),
        PopulateParams { floor: -20, step: 1, count: 40 },
    );
    let _uint_linear_hist = populated(
        root.create_uint_linear_histogram(
            unique_name("histogram"),
            LinearHistogramParams { floor: 5, step_size: 5, buckets: 3 },
        ),
        PopulateParams { floor: 0, step: 1, count: 40 },
    );
    let _double_linear_hist = populated(
        root.create_double_linear_histogram(
            unique_name("histogram"),
            LinearHistogramParams { floor: 0.0, step_size: 0.5, buckets: 3 },
        ),
        PopulateParams { floor: -1.0, step: 0.1, count: 40 },
    );

    let _int_exp_hist = populated(
        root.create_int_exponential_histogram(
            unique_name("histogram"),
            ExponentialHistogramParams {
                floor: -10,
                initial_step: 5,
                step_multiplier: 2,
                buckets: 3,
            },
        ),
        PopulateParams { floor: -20, step: 1, count: 40 },
    );
    let _uint_exp_hist = populated(
        root.create_uint_exponential_histogram(
            unique_name("histogram"),
            ExponentialHistogramParams {
                floor: 0,
                initial_step: 1,
                step_multiplier: 2,
                buckets: 3,
            },
        ),
        PopulateParams { floor: 0, step: 1, count: 40 },
    );
    let _double_exp_hist = populated(
        root.create_double_exponential_histogram(
            unique_name("histogram"),
            ExponentialHistogramParams {
                floor: 0.0,
                initial_step: 1.25,
                step_multiplier: 3.0,
                buckets: 3,
            },
        ),
        PopulateParams { floor: -1.0, step: 0.1, count: 40 },
    );

    let mut fs = ServiceFs::new();
    // NOTE: this FIDL service is deprecated and the following *should not* be done.
    // Rust doesn't have a way of writing to the deprecated FIDL service, therefore
    // we read what we wrote to the VMO and provide it through the service for testing
    // purposes.
    fs.dir("objects").add_fidl_service(move |stream| {
        spawn_inspect_server(stream, example_table.get_node_object());
    });
    inspector.export(&mut fs);
    fs.take_and_serve_directory_handle()?;

    Ok(fs.collect().await)
}
