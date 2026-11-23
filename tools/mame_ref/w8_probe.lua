local frame=0
local function log(s) emu.print_info(s) end
local cpu=manager.machine.devices[":maincpu"]
local space=cpu.spaces["program"]
local tap=space:install_write_tap(0xFFE0F0,0xFFE0F7,"w",function(offset,data,mask)
  if frame>=1195 and frame<=1211 then log(string.format("MW8 %06X=%02X m=%02X f=%d",offset,data,mask,frame)) end
end)
emu.register_frame_done(function() frame=frame+1; tap:reinstall() end,"frame")
