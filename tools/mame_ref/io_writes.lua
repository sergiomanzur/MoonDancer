local frame=0
local function log(s) emu.print_info(s) end
local cpu=manager.machine.devices[":maincpu"]
local space=cpu.spaces["program"]
local tap=space:install_write_tap(0xE40000,0xE4001F,"io",function(offset,data,mask)
  if frame>=1100 and frame<=1210 then
    log(string.format("IOW %06X=%04X m=%04X PC=%06X f=%d",offset,data,mask,cpu.state["CURPC"].value,frame))
  end
end)
emu.register_frame_done(function() frame=frame+1; tap:reinstall() end,"frame")
