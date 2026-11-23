local frame=0
local function log(s) emu.print_info(s) end
local cpu=manager.machine.devices[":maincpu"]
local space=cpu.spaces["program"]
local rtap=space:install_read_tap(0xE40000,0xE4003F,"io",function(offset,data,mask)
  if offset==0xE40008 then
    if frame>=900 and frame<940 then data=data & ~0x01 end
    if frame>=1000 and frame<1040 then data=data & ~0x10 end
  end
  return data,mask
end)
emu.register_frame_done(function()
 frame=frame+1; rtap:reinstall()
 local sh=manager.machine.memory.shares[":workram"]
 local h=1469598103934665603
 for i=0,0x3E00-1 do h=(h ~ sh:read_u8(i)) * 1099511628211 end
 -- Lua 5.3 integers are 64-bit; keep unsigned modulo 2^64
 log(string.format("WH %d %016X",frame,h & 0xFFFFFFFFFFFFFFFF))
end,"frame")
