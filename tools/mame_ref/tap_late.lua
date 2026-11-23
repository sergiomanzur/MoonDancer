local frame=0
local tap=nil
local n=0
local function log(s) emu.print_info("PROBE "..s) end
local cpu=manager.machine.devices[":maincpu"]
local space=cpu.spaces["program"]
local function onwrite(offset,data,mask)
  if n<30 then log(string.format("W off=%06X data=%04X mask=%04X f=%d",offset,data,mask,frame)); n=n+1 end
end
emu.register_frame_done(function()
  frame=frame+1
  if frame==2 and tap==nil then
    tap=space:install_write_tap(0xFFE000,0xFFFFFF,"wr2",onwrite)
    log("installed tap at frame 2")
  elseif tap~=nil then tap:reinstall() end
end,"frame")
