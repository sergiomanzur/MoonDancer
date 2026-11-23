local frame=0
local function log(s) emu.print_info(s) end
local cpu=manager.machine.devices[":maincpu"]
local space=cpu.spaces["program"]
local tap=space:install_read_tap(0xE40000,0xE4003F,"io",function(offset,data,mask)
  if frame>=1000 and frame<1002 then
    log(string.format("RD %06X=%04X m=%04X PC=%06X",offset,data,mask,cpu.state["CURPC"].value))
  end
end)
emu.register_frame_done(function()
 frame=frame+1
 tap:reinstall()
end,"frame")
