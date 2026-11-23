local frame = 0
local function log(s) emu.print_info(s) end
local cpu = manager.machine.devices[":maincpu"]
local space = cpu.spaces["program"]
local rtap = space:install_read_tap(0xE40000, 0xE4003F, "io", function(offset,data,mask)
  if offset == 0xE40008 then
    if frame >= 900 and frame < 940 then data = data & ~0x01 end
    if frame >= 1000 and frame < 1040 then data = data & ~0x10 end
  end
  return data, mask
end)

local stub = {
  0x41F9,0x00C0,0x0004, 0x30FC,0x0000, 0x30FC,0x0000,
  0x43F9,0x00C0,0x0000, 0x45F9,0x0040,0x0000,
  0x303C,0x7FFF,
  0x3211, 0x34C1, 0x51C8,0xFFFA, 0x60FE
}

emu.register_frame_done(function()
  frame = frame + 1
  rtap:reinstall()
  if frame == 1200 then
    local addr = 0xFFFFC800
    for _, w in ipairs(stub) do
      space:write_u16(addr, w)
      addr = addr + 2
    end
    cpu.state["PC"].value = 0xFFFFC800
    log("VDP dump stub installed")
  end
  if frame == 1204 then
    local sh = manager.machine.memory.shares[":tileram"]
    local f = io.open("build/mame_vdp_vram.bin", "wb")
    if f then
      for i = 0, sh.size - 1 do f:write(string.char(sh:read_u8(i))) end
      f:close()
      log("wrote build/mame_vdp_vram.bin")
    end
    local w = manager.machine.memory.shares[":workram"]
    local f2 = io.open("build/mame_vdp_workram.bin", "wb")
    if f2 then
      for i = 0, w.size - 1 do f2:write(string.char(w:read_u8(i))) end
      f2:close()
      log("wrote build/mame_vdp_workram.bin")
    end
  end
end, "frame")
