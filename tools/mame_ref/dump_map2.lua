local frame=0
local function log(s) emu.print_info(s) end
emu.register_frame_done(function()
 frame=frame+1
 if frame~=900 then return end
 local cpu=manager.machine.devices[":maincpu"]
 for name,space in pairs(cpu.spaces) do
   log("SPACE "..name.." index="..space.index.." width="..space.data_width)
   if space.map then
     log(" entries="..#space.map.entries)
     for i,e in ipairs(space.map.entries) do
       local r=e.read; local w=e.write
       log(string.format("  %06X-%06X mir=%06X mask=%04X R=%s W=%s share=%s region=%s",
         e.address_start,e.address_end,e.address_mirror,e.address_mask,
         r and (tostring(r.handlertype).."/"..tostring(r.name)) or "nil",
         w and (tostring(w.handlertype).."/"..tostring(w.name)) or "nil",
         tostring(e.share),tostring(e.region)))
     end
   end
 end
end,"frame")
