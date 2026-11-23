local frame = 0
local function log(s) emu.print_info(s) end
local cpu = manager.machine.devices[":maincpu"]
local space = cpu.spaces["program"]
local stub = {
  0x41F9,0x00C0,0x0004, 0x30FC,0x0000, 0x30FC,0x0000,
  0x43F9,0x00C0,0x0000, 0x45F9,0x0040,0x0000,
  0x303C,0x7FFF, 0x3211, 0x34C1, 0x51C8,0xFFFA, 0x60FE
}
emu.register_frame_done(function()
  frame = frame + 1
  if frame == 1200 then
    local addr = 0xFFFFC800
    for _, w in ipairs(stub) do space:write_u16(addr, w); addr = addr + 2 end
    log("old PC="..string.format("%06X",cpu.state["PC"].value))
    cpu.state["PC"].value = 0xFFFFC800
    log("new PC="..string.format("%06X",cpu.state["PC"].value))
  end
  if frame == 1201 or frame == 1204 then
    local sh = manager.machine.memory.shares[":tileram"]
    log(string.format("frame %d PC=%06X tileram0=%04X %04X",frame,cpu.state["PC"].value,sh:read_u16(0),sh:read_u16(2)))
  end
  if frame == 1204 then
    local sh = manager.machine.memory.shares[":tileram"]
    local f = io.open("build/mame_vdp_vram.bin","wb")
    for i=0,sh.size-1 do f:write(string.char(sh:read_u8(i))) end
    f:close(); log("wrote vram")
  end
end,"frame")
