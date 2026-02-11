//! rust_rainbow.wasm — Smooth rainbow with println! output.
//!
//! Same HSV rainbow as the C example, but written in Rust and using
//! standard println!() via WASI fd_write to demonstrate that printf-style
//! output works from Rust WASM modules.

use std::sync::atomic::{AtomicI32, Ordering};

// ConeZ host imports (module "env")
extern "C" {
    fn led_set_pixel_hsv(channel: i32, pos: i32, h: i32, s: i32, v: i32);
    fn led_show();
    fn led_count(channel: i32) -> i32;
    fn delay_ms(ms: i32);
}

static OFFSET: AtomicI32 = AtomicI32::new(0);
static FRAME: AtomicI32 = AtomicI32::new(0);

#[no_mangle]
pub extern "C" fn setup() {
    println!("rust_rainbow: starting");
}

#[export_name = "loop"]
pub extern "C" fn wasm_loop() {
    let offset = OFFSET.load(Ordering::Relaxed);
    let frame = FRAME.load(Ordering::Relaxed);

    unsafe {
        let n = led_count(1);
        for i in 0..n {
            let hue = ((i * 256 / n) + offset) & 0xFF;
            led_set_pixel_hsv(1, i, hue, 255, 180);
        }
        led_show();
        delay_ms(30);
    }

    if frame % 100 == 0 {
        println!("rust_rainbow: frame {}", frame);
    }

    OFFSET.store((offset + 2) & 0xFF, Ordering::Relaxed);
    FRAME.store(frame + 1, Ordering::Relaxed);
}
