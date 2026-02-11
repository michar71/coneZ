//! rust_rgb_cycle.wasm — #![no_std] RGB cycle for ConeZ WASM runtime.
//!
//! Same logic as the C rgb_cycle example, but written in no_std Rust
//! targeting wasm32-unknown-unknown (no WASI). Demonstrates the
//! bare-metal approach for minimal binary size.

#![no_std]

use core::panic::PanicInfo;

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    core::arch::wasm32::unreachable()
}

// ConeZ host imports (module "env")
extern "C" {
    fn led_fill(channel: i32, r: i32, g: i32, b: i32);
    fn led_show();
    fn print_i32(val: i32);
    fn print_str(ptr: *const u8, len: i32);
    fn delay_ms(ms: i32);
}

static mut FRAME: i32 = 0;

#[no_mangle]
pub extern "C" fn setup() {
    let msg = b"rust_rgb_cycle: starting\n";
    unsafe { print_str(msg.as_ptr(), msg.len() as i32) };
}

#[export_name = "loop"]
pub extern "C" fn wasm_loop() {
    let frame = unsafe { FRAME };

    let phase = frame % 3;
    unsafe {
        match phase {
            0 => led_fill(1, 255, 0, 0),   // red
            1 => led_fill(1, 0, 255, 0),    // green
            _ => led_fill(1, 0, 0, 255),    // blue
        }
        led_show();

        print_i32(frame);
        print_str(b" ".as_ptr(), 1);

        FRAME = frame + 1;
        delay_ms(500);
    }
}
