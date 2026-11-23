local frame=0
local function log(s) emu.print_info(s) end
emu.register_frame_done(function()
 frame=frame+1
 if frame==1200 then
   local dev=manager.machine.devices[":mcu"]
   if dev and dev.state then
     local names={}
     for name,entry in pairs(dev.state) do names[#names+1]=name end
     table.sort(names)
     local t={}
     for _,name in ipairs(names) do
       local v=dev.state[name].value
       if type(v)=="number" then t[#t+1]=string.format("%s=%X",name,v) else t[#t+1]=name.."="..tostring(v) end
     end
     log("MCUSTATE "..table.concat(t," "))
   end
 end
end,"frame")
