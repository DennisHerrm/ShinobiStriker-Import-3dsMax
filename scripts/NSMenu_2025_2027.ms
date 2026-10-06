-- ============================================================
--  NSMenu_2025_2027.ms - Menueeintrag ab 3ds Max 2025
--
--  Ab 2025 entstehen Menues nur im Callback #cuiRegisterMenus
--  (CuiMenuManager). Laeuft als PRE-Start-up-Skript, sonst meldet
--  sich der Callback zu spaet an (Lehre aus dem SWBF2 Import).
--  647394 = Action-Table der MacroScripts; permanentId = Makro`Kategorie.
--  Eigene GUIDs - sie duerfen mit keinem anderen Paket kollidieren.
-- ============================================================
(
  local sSubMenuGuid = "EEDFE909-BAA2-4772-8164-4F46D764521A"
  local sActionGuid  = "547E4674-9EA4-407B-9EB7-001F706D0253"
  local sAnimGuid    = "8FDD7EAA-706E-4400-A79A-2233D1A5519D"

  fn NSRegisterMenu =
  (
    local mgr = callbacks.notificationParam()
    if (mgr != undefined) then
    (
      try
      (
        local mnuSub = mgr.mainMenuBar.CreateSubMenu sSubMenuGuid "Shinobi Striker Tool"
        if (mnuSub != undefined) then
        (
          mnuSub.CreateAction sActionGuid 647394 "NSImport_Open`Shinobi Striker Tool"
          mnuSub.CreateAction sAnimGuid 647394 "NSImport_Anim`Shinobi Striker Tool"
        )
        else
          format "[ShinobiStriker] CreateSubMenu returned undefined - GUID already taken?\n"
      )
      catch (format "[ShinobiStriker] menu could not be created: %\n" (getCurrentException()))
    )
  )

  callbacks.removeScripts id:#nsImportMenu
  callbacks.addScript #cuiRegisterMenus NSRegisterMenu id:#nsImportMenu
)
