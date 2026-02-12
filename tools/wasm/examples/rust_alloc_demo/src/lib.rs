//! rust_alloc_demo.wasm — no_std + alloc demo using ConezAllocator.

#![no_std]

extern crate alloc;

use alloc::string::String;
use alloc::vec::Vec;
use core::panic::PanicInfo;

mod conez {
    #![allow(dead_code)]
    include!("../../../conez_api.rs");
}

use conez::*;

#[global_allocator]
static GLOBAL: ConezAllocator = ConezAllocator;

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    core::arch::wasm32::unreachable()
}

static mut FRAME: i32 = 0;

#[no_mangle]
pub extern "C" fn setup() {
    let mut s = String::from("rust_alloc_demo: allocator ready, frame=");
    s.push('0');
    s.push('\n');
    print(&s);
}

#[export_name = "loop"]
pub extern "C" fn wasm_loop() {
    let n = unsafe { led_count(1) };
    if n <= 0 {
        unsafe { delay_ms(250) };
        return;
    }

    let count = n as usize;
    let frame = unsafe { FRAME };
    let mut rgb: Vec<u8> = Vec::with_capacity(count * 3);
    for i in 0..count {
        let phase = ((i as i32 + frame) & 0xFF) as u8;
        rgb.push(phase);
        rgb.push(255u8.wrapping_sub(phase));
        rgb.push(phase >> 1);
    }

    unsafe {
        led_set_buffer(1, rgb.as_ptr(), n);
        led_show();
        delay_ms(60);
        FRAME = frame + 1;
    }
}
