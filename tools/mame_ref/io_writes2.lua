local frame=0
local function log(s) emu.print_info(s) end
local cpu=manager.machine.devices[":maincpu"]
local space=cpu.spaces["program"]
local tap=space:install_write_tap(0xE40000,0xE4001F,"io",function(offset,data,mask)
  if offset==0xE4000E or offset==0xE4001E or offset==0xE4001F then
    log(string.format("MIOW %06X=%04X m=%04X PC=%06X f=%d",offset,data,mask,cpu.state["CURPC"].value,frame))
  end
end)
emu.register_frame_done(function() frame=frame+1; tap:reinstall() end,"frame")
