local frame=0
local n=0
local function log(s) emu.print_info("PROBE "..s) end
local cpu=manager.machine.devices[":maincpu"]
local space=cpu.spaces["program"]
local tap=space:install_read_tap(0xE40000,0xE4003F,"rd",function(offset,data,mask)
  if n<30 then log(string.format("R off=%06X data=%04X f=%d",offset,data,frame)); n=n+1 end
  if offset==0xE40008 and frame>=936 and frame<=942 then log(string.format("SVC f=%d data=%04X",frame,data)) end
  return data,mask
end)
emu.register_frame_done(function() frame=frame+1; tap:reinstall() end,"frame")
