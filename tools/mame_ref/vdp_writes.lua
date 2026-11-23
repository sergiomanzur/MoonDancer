local frame=0
local function log(s) emu.print_info(s) end
local cpu=manager.machine.devices[":maincpu"]
local space=cpu.spaces["program"]
local tap=space:install_write_tap(0xC00000,0xC0000F,"vdp",function(offset,data,mask)
  if frame>=1180 and frame<=1200 then
    log(string.format("VW %06X=%04X m=%04X PC=%06X f=%d",offset,data,mask,cpu.state["CURPC"].value,frame))
  end
end)
emu.register_frame_done(function()
 frame=frame+1
 tap:reinstall()
end,"frame")
