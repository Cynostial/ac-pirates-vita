-- Coin-store entry points now open the game's own challenge popup. Contracts
-- use the existing gameplay counters, eligibility rules, reward values and save
-- format; no new currency or platform overlay is introduced.
local function entity(tag)return MainProcess:GetEntity(tag)end
local function load_cards()
    -- These small original UI classes are normally registered with gameplay.
    -- Store access also exists before a playground has ever been loaded.
    local previous=Loading_CurrentModule
    Loading_CurrentModule="__Spark2"
    for _,v in ipairs{
        {"Popup","Core/GUI/Utils/Popup.lua"},
        {"DailyMissionPopupFrame","Core/NewGUI/Widgets/DailyMissionWidget/DailyMissionPopupFrame.lua"},
        {"DailyMissionPopup","Core/NewGUI/Widgets/DailyMissionWidget/DailyMissionPopup.lua"},
    } do
        if not ClassDefinitionMap[v[1]] then
            dofile("ACPirates_Demo/"..v[2])
        end
    end
    Loading_CurrentModule=previous
end
local function renew_claimed(o)
    local changed={}
    o.PreviousChallengesInfo={}
    for slot=1,o.DailyChallengesSlots do
        local c=o.DailyChallengeGroup[slot]
        if c and c.IsAlreadyRewarded then
            o.PreviousChallengesInfo[slot]={UniqueID=c.UniqueID,Difficulty=c.Difficulty}
            c:Destroy();o.DailyChallengeGroup[slot]=false
            o._SaveData.ChallengesSlots[slot].BufferedChallenge=false
            o:_GenerateDailyChallenge(slot)
            changed[#changed+1]=slot
        end
    end
    if #changed>0 then
        entity("TagPlayerProfile"):SaveDailyChallengesData()
        entity("TagEventManager"):DispatchEvent("OnDC_Generated",changed)
    end
end
function ACPVitaEconomy(def,ui)
    local name=def.ClassName
    if name=="NodeMap" then def.StarterPack=function()end end
    if name=="InAppManager" then
        for _,method in ipairs{"OpenStore","CloseStore","BuyItem","CheckUncreditedItems",
            "LaunchRestoreItems","RestorePurchase","CreditPurchases","UpdateButtonsInfos"} do
            def[method]=function()end
        end
        def.CheckConnection=function()return false end
        return true
    elseif name=="InAppPopup" then
        -- Every former store caller still gets an entity and its close callback.
        -- No price cards, adverts, restore button or network spinner are built.
        for key in pairs(def) do def[key]=nil end
        def.ClassName=name;def[1]="PakGUITransform2D";def[2]="PakUpdatable";def[3]="TagInAppPopup"
        def.HiddenCallback=false;def.Depth=-15
        def.OnPopupClosed=function()end
        def.PopupManager={RemovePopup=function()end}
        def.ShowPopup=function(o)o._VitaOpenContracts=true end
        def.OnUpdate=function(o)
            if o._VitaCloseContracts then
                o._VitaCloseContracts=false
                local prompt=o.Process:GetEntity("TagNodeMapTravelPopup")
                if prompt and not prompt.IsDestroyed then prompt:RefreshBuySideQuest() end
                if o.HiddenCallback then o:HiddenCallback() end
                o:Destroy();return
            end
            if not o._VitaOpenContracts then return end
            o._VitaOpenContracts=false
            load_cards()
            -- The world map deliberately destroys PopupLayer. Parent the native
            -- cards to the former store's own GUI transform instead, so this
            -- works from the map, ship/tavern menus and gameplay alike.
            local popup=o:CreateEntity("DailyMissionPopup")
            popup._VitaCoinSource=o
            popup._VitaPreviouslyPaused=GP and GP._Pause
            o._VitaContracts=popup
            popup:UpdatePopupInformation();popup:ShowPopup()
            ACPVitaLog("Coin store replaced by native contracts")
        end
        def.HidePopup=function(o)
            if o._VitaContracts and not o._VitaContracts.IsDestroyed then
                o._VitaContracts.PopUpContent.CloseButton:OnButtonReleased()
            end
        end
        return true
    elseif name=="DailyChallengesManager" then
        local reward=def.OnDC_Rewarded
        def.OnDC_Rewarded=function(o,c)
            -- The mobile callback trusts its UI. Check membership/completion
            -- here as well so repeated pad/touch events cannot duplicate a payout.
            if not c or c.IsDestroyed or not c.IsCompleted or c.IsAlreadyRewarded
                or o.DailyChallengeGroup[c.Slot]~=c then return end
            o._SaveData.ChallengesSlots[c.Slot].BufferedChallenge=true
            reward(o,c) -- pays, replaces this slot, dispatches and saves normally
            ACPVitaLog("Contract reward earned through gameplay")
        end
        ui.after(def,"DeserializeData",renew_claimed)
        def.OnUpdate=function(o)
            -- Contracts do not expire while the player is away or wait for a
            -- wall-clock reset. The normal gameplay event counters still run.
            for _,c in pairs(o.DailyChallengeGroup) do
                if c and c.Update then c:Update() end
            end
        end
        return true
    elseif name=="DailyMissionPopup" then
        local content=def.PopUpContent
        content.PopupTitle.Text="EARN COINS"
        content.PopupTitle.IsAlreadyLocalized=true
        -- Keep the original three cards and their gold/progress/claim visuals.
        -- Remove the daily timer and paid refresh UI before construction.
        content.RemainingTimer=nil
        content.ReloadButton=nil
        local close_button=content.CloseButton.OnButtonReleased
        content.CloseButton.OnButtonReleased=function(button)
            if button.Parent.Parent._VitaCoinSource then
                entity("TagGameSound"):CallEventFiltered("Button_Cancel")
                button.Parent.Parent:ClosePopup()
            else close_button(button) end
        end
        local close_popup=def._DMPopup_ClosePopup
        def._DMPopup_ClosePopup=function(o)
            local paused=o._VitaCoinSource and o._VitaPreviouslyPaused
            close_popup(o)
            if paused then PauseGame() end
        end
        local update=def.UpdatePopupInformation
        def.UpdatePopupInformation=function(o,...)
            -- The original layout helper only needs ResetTimer from this child.
            o.PopUpContent.RemainingTimer={Timer={ResetTimer=function()end}}
            ui.focus(o,nil)
            update(o,...)
            o._VitaNavEntries={}
            for _,frame in ipairs(o.PopUpContent.MissionsFrames) do
                frame:DoNecessaryTextureChange()
                o._VitaNavEntries[#o._VitaNavEntries+1]=frame.BottomButton
            end
            o._VitaNavEntries[#o._VitaNavEntries+1]=o.PopUpContent.CloseButton
        end
        def.OnShow=function(o)
            o:SetEnable(true);o.IsOpen=true
            -- Hide the container's black texture, as the original popup does;
            -- its independently rendered children remain visible.
            o.PopUpContent.Visible=false
            o._VitaNavTransparentContainer=o.PopUpContent
            for _,frame in ipairs(o.PopUpContent.MissionsFrames) do frame:DoNecessaryTextureChange() end
            o._PopupButtonBindedToDeviceBackButton=o.PopUpContent.CloseButton
        end
        def.OnClose=function(o)
            ui.focus(o,nil)
            for _,frame in ipairs(o.PopUpContent.MissionsFrames) do
                local c=frame.MissionPointedAt
                if c and not c.IsDestroyed then c.Noticed=true end
                frame:Destroy()
            end
            o.PopUpContent.MissionsFrames={};o.IsOpen=false
            entity("TagPlayerProfile"):SaveDailyChallengesData()
            local button=GP and GP:GetEntity("TagDailyChallengeButton")
            if button then button.ChallangesAvailable=false;button.RewardsAvailable=false end
            local source=o._VitaCoinSource
            if source and not source.IsDestroyed then source._VitaCloseContracts=true end
        end
        for _,event in ipairs{"OnDC_Generated","OnDC_Completed"} do
            local original=def[event]
            def[event]=function(o,...)
                ui.focus(o,nil)
                return original(o,...)
            end
        end
        ui.after(def,"OnUpdate",function(o)
            local manager=entity("TagDeviceBackButtonManager")
            local top=manager._PopupStack and manager._PopupStack[1]
            ui.navigation(o,o.IsOpen and o.RefreshState=="Idle" and top==o,true)
        end)
        return true
    end
    return false
end
