#[cxx::bridge(namespace = "ffi::lib")]
mod ffi {
    extern "Rust" {
        fn print_simple_log();
    }
}

fn print_simple_log() {
    println!("Hello from Rust!");
}
