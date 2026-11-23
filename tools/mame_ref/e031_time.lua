local frame=0
local tap=nil
local n=0
local function log(s) emu.print_info("PROBE "..s) end
local cpu=manager.machine.devices[":maincpu"]
local space=cpu.spaces["program"]
local function onwrite(offset,data,mask)
  local a=offset & 0x3FFF
  if (a==0x2030 or a==0x2031) and n<25 then
    log(string.format("W31 off=%06X data=%04X mask=%04X f=%d time=%s",offset,data,mask,frame,tostring(manager.machine.time)))
    n=n+1
  end
end
emu.register_frame_done(function()
  frame=frame+1
  if frame==2 and tap==nil then
    tap=space:install_write_tap(0xFFE000,0xFFFFFF,"wr3",onwrite)
  elseif tap~=nil then tap:reinstall() end
end,"frame")
