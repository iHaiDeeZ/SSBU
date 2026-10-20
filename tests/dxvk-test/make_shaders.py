"""DXBC for the DXVK test app (main.cpp), compiled by Windows' own d3dcompiler_47.dll: shaders.h.

Run on Windows: python tests/dxvk-test/make_shaders.py"""
import ctypes
import os
import sys

HLSL = r"""
cbuffer Constants : register(b0) {
    float angle;
    float aspect;
    float2 pad;
    float4 tint;
};

struct V { float4 pos : SV_Position; float3 color : COLOR; };

V vs_main(uint id : SV_VertexID) {
    static const float3 colors[3] = { float3(1, 0, 0), float3(0, 1, 0), float3(0, 0, 1) };
    float a = angle + id * 2.0943951;
    V v;
    v.pos = float4(0.6 * sin(a) * aspect, 0.6 * cos(a), 0, 1);
    v.color = colors[id];
    return v;
}

float4 ps_main(V v) : SV_Target {
    return float4(lerp(v.color, tint.rgb, 0.25), 1);
}
"""


class Blob(ctypes.Structure):
    pass


def compile_one(d3d, entry, target):
    code = ctypes.c_void_p()
    errors = ctypes.c_void_p()
    src = HLSL.encode()
    hr = d3d.D3DCompile(src, len(src), b"test.hlsl", None, None, entry.encode(), target.encode(), 0, 0,
                        ctypes.byref(code), ctypes.byref(errors))
    def blob_bytes(p):
        vt = ctypes.cast(p, ctypes.POINTER(ctypes.POINTER(ctypes.c_void_p))).contents
        get_ptr = ctypes.WINFUNCTYPE(ctypes.c_void_p, ctypes.c_void_p)(vt[3])
        get_size = ctypes.WINFUNCTYPE(ctypes.c_size_t, ctypes.c_void_p)(vt[4])
        return ctypes.string_at(get_ptr(p), get_size(p))
    if hr != 0:
        raise SystemExit(f"{entry}: 0x{hr & 0xffffffff:08x}\n" + (blob_bytes(errors).decode(errors="replace") if errors else ""))
    return blob_bytes(code)


def main():
    d3d = ctypes.WinDLL("d3dcompiler_47.dll")
    out = ["// made by make_shaders.py (d3dcompiler_47): do not edit", "#pragma once", "#include <cstdint>"]
    for name, entry, target in (("kVS", "vs_main", "vs_5_0"), ("kPS", "ps_main", "ps_5_0")):
        data = compile_one(d3d, entry, target)
        body = ",".join(f"0x{b:02x}" for b in data)
        out.append(f"static const uint8_t {name}[{len(data)}] = {{{body}}};")
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "shaders.h")
    with open(path, "w", newline="\n") as f:
        f.write("\n".join(out) + "\n")
    print("wrote", path)


if __name__ == "__main__":
    sys.exit(main())
