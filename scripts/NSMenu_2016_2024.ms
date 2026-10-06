-- ============================================================
--  NSMenu_2016_2024.ms - Menueeintrag fuer 3ds Max 2016 bis 2024
--  (altes menuMan-API, laeuft als Post-Start-up-Skript).
--  Ab 2025 gilt NSMenu_2025_2027.ms; die PackageContents.xml gibt
--  jeder Max-Fassung nur ihr Skript, sonst entstuende das Menue doppelt.
--  Ergebnis: Hauptmenue -> "Shinobi Striker Tool" -> "Import Shinobi Striker"
-- ============================================================
(
  local sMenuName = "Shinobi Striker Tool"
  local mnu = menuMan.findMenu sMenuName
  if (mnu == undefined) then
  (
    local mnuBar = menuMan.getMainMenuBar()
    mnu = menuMan.createMenu sMenuName
    local itmSub = menuMan.createSubMenuItem sMenuName mnu
    mnuBar.addItem itmSub (mnuBar.numItems())
  )
  for aItem in #(#("Import Shinobi Striker", "NSImport_Open"), #("Shinobi Striker Animations", "NSImport_Anim")) do
  (
    local bFound = false
    for i = 1 to mnu.numItems() while (not bFound) do
      if ((mnu.getItem i).getTitle() == aItem[1]) then bFound = true
    if (not bFound) then
    (
      local act = menuMan.createActionItem aItem[2] "Shinobi Striker Tool"
      act.setTitle aItem[1]
      act.setUseCustomTitle true
      mnu.addItem act -1
    )
  )
  menuMan.updateMenuBar()
)
