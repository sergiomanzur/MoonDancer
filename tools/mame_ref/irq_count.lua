local frame=0; local count=0
local function log(s) emu.print_info(s) end
local cpu=manager.machine.devices[":maincpu"]
local space=cpu.spaces["program"]
local tap=space:install_write_tap(0xFFE0CC,0xFFE0CD,"irq",function(offset,data,mask)
  local pc=cpu.state["CURPC"].value
  if pc>=0x1D00 and pc<0x2000 then count=count+1 end
end)
emu.register_frame_done(function()
 frame=frame+1; tap:reinstall()
 if frame==1200 then log("MAME irq count "..count) end
end,"frame")
