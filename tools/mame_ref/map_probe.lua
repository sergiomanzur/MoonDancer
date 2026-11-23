local frame=0
local function log(s) emu.print_info("PROBE "..s) end
local cpu=manager.machine.devices[":maincpu"]
local space=cpu.spaces["program"]
local sh=manager.machine.memory.shares[":workram"]
emu.register_frame_done(function()
  frame=frame+1
  if frame==2 or frame==3 then
    log(string.format("f=%d shareE031=%02X readFFE031=%02X read502031=%02X",frame,sh:read_u8(0x2031),space:read_u8(0xFFE031),space:read_u8(0x502031)))
  end
end,"frame")
