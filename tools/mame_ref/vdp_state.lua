local frame=0
local function log(s) emu.print_info(s) end
emu.register_frame_done(function()
 frame=frame+1
 if frame==1200 then
   local dev=manager.machine.devices[":gen_vdp"]
   log("dev="..tostring(dev))
   if dev and dev.state then
     for k,v in pairs(dev.state) do log("VDPSTATE "..tostring(k).." "..tostring(v.value)) end
   else
     log("no state table")
   end
 end
end,"frame")
