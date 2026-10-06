-- ============================================================
--  NSImport.mcr - Menue "Shinobi Striker Tool"
--    "Import Shinobi Striker"       das Figurenfenster   (ShinobiCpp.showDialog)
--    "Shinobi Striker Animations"   das Animationsfenster (ShinobiCpp.showAnimDialog)
-- ============================================================

macroScript NSImport_Open
    category:"Shinobi Striker Tool"
    buttonText:"Import Shinobi Striker"
    toolTip:"Naruto to Boruto: Shinobi Striker - import a character straight from the game"
(
    on execute do
    (
        local oJs = undefined
        try (if (ShinobiCpp != undefined) then oJs = ShinobiCpp) catch (oJs = undefined)
        if (oJs == undefined) then
            try (for i in getCoreInterfaces() where matchPattern (i as string) pattern:"*ShinobiCpp*" do oJs = i) catch ()
        if (oJs != undefined) then
            oJs.showDialog()
        else
            messageBox "NSImport.dlu is not loaded.\n\nPlease run INSTALLIERE.bat again and restart 3ds Max." title:"Shinobi Striker Tool"
    )
)

macroScript NSImport_Anim
    category:"Shinobi Striker Tool"
    buttonText:"Shinobi Striker Animations"
    toolTip:"Naruto to Boruto: Shinobi Striker - load animations onto the character in the scene"
(
    on execute do
    (
        local oJs = undefined
        try (if (ShinobiCpp != undefined) then oJs = ShinobiCpp) catch (oJs = undefined)
        if (oJs == undefined) then
            try (for i in getCoreInterfaces() where matchPattern (i as string) pattern:"*ShinobiCpp*" do oJs = i) catch ()
        if (oJs != undefined) then
            oJs.showAnimDialog()
        else
            messageBox "NSImport.dlu is not loaded.\n\nPlease run INSTALLIERE.bat again and restart 3ds Max." title:"Shinobi Striker Tool"
    )
)
