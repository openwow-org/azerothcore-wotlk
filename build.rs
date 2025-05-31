#[allow(unused_must_use)]
fn main() {
    cxx_build::bridge("src/lib.rs").compile("azerothcore");
    println!("cargo:rerun-if-changed=src/lib.rs");
}
