local frame=0
local function log(s) emu.print_info(s) end
emu.register_frame_done(function()
 frame=frame+1
 if frame~=900 then return end
 local cpu=manager.machine.devices[":maincpu"]
 local m=cpu.spaces["program"]
 for _,a in ipairs({0xFE0000,0xFE0020,0xFE003E,0xE40000,0xE40006,0xE4000E,0xC00000,0xC00004,0x440000,0x840000,0x400000,0x410000,0xFFC000}) do
   local ok,v = pcall(function() return m:read_u16(a) end)
   local ok2,v2 = pcall(function() return m:read_direct_u16(a) end)
   log(string.format("%06X read=%s direct=%s",a, ok and string.format("%04X",v) or "ERR", ok2 and string.format("%04X",v2) or "ERR"))
 end
end,"frame")
