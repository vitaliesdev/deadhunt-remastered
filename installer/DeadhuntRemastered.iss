; Deadhunt Remastered - Inno Setup script.
; Installs only files that do not come with the game (build\dist\, made by tools/build/stage.py). The game's own
; files are never shipped and stay as they are: DeadhuntFX applies its changes in memory. The only
; game files touched are GtEngine.ini (settings) and, with OpenAL Soft, OpenAL32.dll; both are copied
; to DeadhuntFX\original first and put back on uninstall.
; Build: build_release.bat (DLL + build\dist + this)  ->  build\release\DeadhuntRemastered_Setup_v<version>.exe
; The version comes from the VERSION file at the repo root. Installing a newer (or the same) version over
; an existing install updates it in place and keeps the player's settings.

#define AppName "Deadhunt Remastered"
#define VerFile FileOpen(AddBackslash(SourcePath) + "..\VERSION")
#define AppVer Trim(FileRead(VerFile))
#expr FileClose(VerFile)
#define Dist "..\build\dist"

[Setup]
AppId={{6B1E2C7A-3D4F-4E8B-9A21-DEADB0A7F2E1}
AppName={#AppName}
AppVersion={#AppVer}
AppVerName={#AppName} {#AppVer}
AppPublisher=REPTIX
AppPublisherURL=https://steamcommunity.com/id/reptilemk/
AppCopyright=Mod (c) 2026 REPTIX. Deadhunt (c) 2005 REL Games.
VersionInfoVersion={#AppVer}
VersionInfoProductVersion={#AppVer}
VersionInfoProductName={#AppName}
VersionInfoDescription={#AppName} {#AppVer} - installer
DefaultDirName={code:GameDir}
AppendDefaultDirName=no
DirExistsWarning=no
DisableProgramGroupPage=yes
UsePreviousAppDir=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
UninstallFilesDir={app}\DeadhuntFX\desinstalar
UninstallDisplayName={#AppName}
UninstallDisplayIcon={app}\Deadhunt.exe
SetupIconFile=images\icono.ico
WizardStyle=modern
WizardImageFile=images\lateral_100.bmp,images\lateral_200.bmp
WizardSmallImageFile=images\pequena_100.bmp,images\pequena_200.bmp
Compression=lzma2/ultra64
SolidCompression=yes
OutputDir=..\build\release
OutputBaseFilename=DeadhuntRemastered_Setup_v{#AppVer}
ShowLanguageDialog=yes
UsePreviousLanguage=no
LanguageDetectionMethod=none
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"; LicenseFile: "legal\aviso_en.txt"
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"; LicenseFile: "legal\aviso_es.txt"
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"; LicenseFile: "legal\aviso_ru.txt"
Name: "french"; MessagesFile: "compiler:Languages\French.isl"; LicenseFile: "legal\aviso_fr.txt"
Name: "german"; MessagesFile: "compiler:Languages\German.isl"; LicenseFile: "legal\aviso_de.txt"
Name: "italian"; MessagesFile: "compiler:Languages\Italian.isl"; LicenseFile: "legal\aviso_it.txt"
Name: "brazilianportuguese"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"; LicenseFile: "legal\aviso_pt.txt"
Name: "polish"; MessagesFile: "compiler:Languages\Polish.isl"; LicenseFile: "legal\aviso_pl.txt"
Name: "ukrainian"; MessagesFile: "compiler:Languages\Ukrainian.isl"; LicenseFile: "legal\aviso_uk.txt"

[CustomMessages]
spanish.LangCode=es
english.LangCode=en
russian.LangCode=ru
spanish.CompMain=Deadhunt Remastered (gráficos, HUD, menús, idiomas, mando)
english.CompMain=Deadhunt Remastered (graphics, HUD, menus, languages, gamepad)
russian.CompMain=Deadhunt Remastered (графика, HUD, меню, языки, геймпад)
spanish.CompDxvk=DXVK: mejor rendimiento en equipos modernos
english.CompDxvk=DXVK: better performance on modern PCs
russian.CompDxvk=DXVK: лучшая производительность на современных ПК
spanish.CompOal=OpenAL Soft: sonido 3D mejorado
english.CompOal=OpenAL Soft: improved 3D sound
russian.CompOal=OpenAL Soft: улучшенный 3D-звук
spanish.NoGame=No se encuentra Deadhunt.exe en esa carpeta. Elige la carpeta donde está instalado Deadhunt (en Steam: clic derecho en el juego > Administrar > Ver archivos locales).
english.NoGame=Deadhunt.exe was not found in that folder. Choose the folder where Deadhunt is installed (in Steam: right-click the game > Manage > Browse local files).
russian.NoGame=В этой папке нет Deadhunt.exe. Выберите папку, где установлен Deadhunt (в Steam: правый клик по игре > Управление > Просмотреть локальные файлы).
spanish.Running=Cierra Deadhunt antes de continuar.
english.Running=Close Deadhunt before continuing.
russian.Running=Закройте Deadhunt перед продолжением.
spanish.PlayNow=Jugar a Deadhunt Remastered
english.PlayNow=Play Deadhunt Remastered
russian.PlayNow=Играть в Deadhunt Remastered
spanish.Credits=Ver los créditos
english.Credits=View the credits
russian.Credits=Открыть титры
french.LangCode=fr
french.CompMain=Deadhunt Remastered (graphismes, HUD, menus, langues, manette)
french.CompDxvk=DXVK : meilleures performances sur les PC modernes
french.CompOal=OpenAL Soft : son 3D amélioré
french.NoGame=Deadhunt.exe est introuvable dans ce dossier. Choisissez le dossier où Deadhunt est installé (dans Steam : clic droit sur le jeu > Gérer > Parcourir les fichiers locaux).
french.Running=Fermez Deadhunt avant de continuer.
french.PlayNow=Jouer à Deadhunt Remastered
french.Credits=Voir les crédits
german.LangCode=de
german.CompMain=Deadhunt Remastered (Grafik, HUD, Menüs, Sprachen, Controller)
german.CompDxvk=DXVK: bessere Leistung auf modernen PCs
german.CompOal=OpenAL Soft: verbesserter 3D-Sound
german.NoGame=Deadhunt.exe wurde in diesem Ordner nicht gefunden. Wählen Sie den Ordner, in dem Deadhunt installiert ist (in Steam: Rechtsklick auf das Spiel > Verwalten > Lokale Dateien durchsuchen).
german.Running=Schließen Sie Deadhunt, bevor Sie fortfahren.
german.PlayNow=Deadhunt Remastered spielen
german.Credits=Mitwirkende anzeigen
italian.LangCode=it
italian.CompMain=Deadhunt Remastered (grafica, HUD, menu, lingue, controller)
italian.CompDxvk=DXVK: prestazioni migliori sui PC moderni
italian.CompOal=OpenAL Soft: audio 3D migliorato
italian.NoGame=Deadhunt.exe non si trova in questa cartella. Scegli la cartella in cui è installato Deadhunt (in Steam: clic destro sul gioco > Gestisci > Sfoglia i file locali).
italian.Running=Chiudi Deadhunt prima di continuare.
italian.PlayNow=Gioca a Deadhunt Remastered
italian.Credits=Visualizza i riconoscimenti
brazilianportuguese.LangCode=pt
brazilianportuguese.CompMain=Deadhunt Remastered (gráficos, HUD, menus, idiomas, controle)
brazilianportuguese.CompDxvk=DXVK: melhor desempenho em PCs modernos
brazilianportuguese.CompOal=OpenAL Soft: som 3D aprimorado
brazilianportuguese.NoGame=Deadhunt.exe não foi encontrado nessa pasta. Escolha a pasta onde o Deadhunt está instalado (na Steam: clique com o botão direito no jogo > Gerenciar > Explorar arquivos locais).
brazilianportuguese.Running=Feche o Deadhunt antes de continuar.
brazilianportuguese.PlayNow=Jogar Deadhunt Remastered
brazilianportuguese.Credits=Ver os créditos
polish.LangCode=pl
polish.CompMain=Deadhunt Remastered (grafika, HUD, menu, języki, kontroler)
polish.CompDxvk=DXVK: lepsza wydajność na nowoczesnych komputerach
polish.CompOal=OpenAL Soft: ulepszony dźwięk 3D
polish.NoGame=W tym folderze nie ma pliku Deadhunt.exe. Wybierz folder, w którym zainstalowano Deadhunt (w Steam: prawy klik na grze > Zarządzaj > Przeglądaj pliki lokalne).
polish.Running=Zamknij Deadhunt przed kontynuowaniem.
polish.PlayNow=Zagraj w Deadhunt Remastered
polish.Credits=Pokaż twórców
ukrainian.LangCode=uk
ukrainian.CompMain=Deadhunt Remastered (графіка, HUD, меню, мови, геймпад)
ukrainian.CompDxvk=DXVK: краща продуктивність на сучасних ПК
ukrainian.CompOal=OpenAL Soft: покращений 3D-звук
ukrainian.NoGame=У цій папці немає Deadhunt.exe. Виберіть папку, де встановлено Deadhunt (у Steam: правий клік на грі > Керувати > Переглянути локальні файли).
ukrainian.Running=Закрийте Deadhunt перед продовженням.
ukrainian.PlayNow=Грати в Deadhunt Remastered
ukrainian.Credits=Переглянути авторів

[Types]
Name: "full"; Description: "{cm:CompMain}"; Flags: iscustom

[Components]
Name: "main"; Description: "{cm:CompMain}"; Types: full; Flags: fixed
Name: "dxvk"; Description: "{cm:CompDxvk}"; Types: full
Name: "openal"; Description: "{cm:CompOal}"; Types: full

[Files]
Source: "{#Dist}\game\d3d9.dll"; DestDir: "{app}"; Components: main; Flags: ignoreversion
Source: "{#Dist}\game\DeadhuntFX.hlsl"; DestDir: "{app}"; Components: main; Flags: ignoreversion
Source: "{#Dist}\game\DeadhuntFX.ini"; DestDir: "{app}"; Components: main; Flags: onlyifdoesntexist uninsneveruninstall
Source: "{#Dist}\game\DeadhuntFX\*"; DestDir: "{app}\DeadhuntFX"; Components: main; Flags: ignoreversion recursesubdirs
Source: "{#Dist}\game\Data\FontE\*"; DestDir: "{app}\Data\FontE"; Components: main; Flags: ignoreversion
Source: "{#Dist}\game\Data\FontP\*"; DestDir: "{app}\Data\FontP"; Components: main; Flags: ignoreversion
Source: "{#Dist}\dxvk\*"; DestDir: "{app}"; Components: dxvk; Flags: ignoreversion
Source: "{#Dist}\openal\OpenAL32.dll"; DestDir: "{app}"; Components: openal; Flags: ignoreversion uninsneveruninstall

[INI]
Filename: "{app}\DeadhuntFX.ini"; Section: "General"; Key: "Language"; String: "{cm:LangCode}"

[InstallDelete]
; when DXVK is not selected, remove a copy left by an earlier install
Type: files; Name: "{app}\dxvk_d3d9.dll"; Components: not dxvk
Type: files; Name: "{app}\dxvk.conf"; Components: not dxvk

[UninstallDelete]
Type: files; Name: "{app}\DeadhuntFX.ini"
Type: files; Name: "{app}\DeadhuntFX.log"
Type: files; Name: "{app}\DeadhuntFX.old.log"
Type: files; Name: "{app}\DeadhuntFX_state.txt"
Type: files; Name: "{app}\DeadhuntFX_hud.log"
Type: files; Name: "{app}\DeadhuntFX_mundo.log"
Type: files; Name: "{app}\DeadhuntFX_cmd.txt"
Type: files; Name: "{app}\DeadhuntFX_captura_*.bmp"
Type: files; Name: "{app}\Deadhunt_d3d9.log"
Type: filesandordirs; Name: "{app}\DeadhuntFX"
Type: filesandordirs; Name: "{app}\Data\FontE"
Type: filesandordirs; Name: "{app}\Data\FontP"

[Run]
Filename: "{app}\Deadhunt.exe"; WorkingDir: "{app}"; Description: "{cm:PlayNow}"; Flags: postinstall nowait skipifsilent
Filename: "{app}\DeadhuntFX\CREDITOS.txt"; Description: "{cm:Credits}"; Flags: postinstall shellexec skipifsilent unchecked

[Code]
function GetSystemMetrics(nIndex: Integer): Integer; external 'GetSystemMetrics@user32.dll stdcall';

{ ---- the game folder: every Steam library, then the usual default }
function FindInLibrary(Lib: String): String;
begin
  Result := '';
  if FileExists(AddBackslash(Lib) + 'steamapps\common\Deadhunt\Deadhunt.exe') then
    Result := AddBackslash(Lib) + 'steamapps\common\Deadhunt';
end;

function GameDir(Param: String): String;
var
  Steam, Vdf, Rest, Path: String;
  Data: AnsiString;
  P, A, B: Integer;
begin
  Result := '';
  if RegQueryStringValue(HKCU, 'Software\Valve\Steam', 'SteamPath', Steam) then begin
    StringChangeEx(Steam, '/', '\', True);
    Result := FindInLibrary(Steam);
    Vdf := AddBackslash(Steam) + 'steamapps\libraryfolders.vdf';
    if (Result = '') and LoadStringFromFile(Vdf, Data) then begin
      Rest := String(Data);
      P := Pos('"path"', Rest);
      while (P > 0) and (Result = '') do begin
        Rest := Copy(Rest, P + 6, Length(Rest));
        A := Pos('"', Rest);
        Rest := Copy(Rest, A + 1, Length(Rest));
        B := Pos('"', Rest);
        Path := Copy(Rest, 1, B - 1);
        StringChangeEx(Path, '\\', '\', True);
        Result := FindInLibrary(Path);
        Rest := Copy(Rest, B + 1, Length(Rest));
        P := Pos('"path"', Rest);
      end;
    end;
  end;
  if Result = '' then Result := ExpandConstant('{commonpf32}\Steam\steamapps\common\Deadhunt');
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if CurPageID = wpSelectDir then begin
    if not FileExists(AddBackslash(WizardDirValue) + 'Deadhunt.exe') then begin
      MsgBox(CustomMessage('NoGame'), mbError, MB_OK);
      Result := False;
    end;
  end;
end;

{ ---- install: keep copies of the two game files that change, then the settings }
{ Copies of the game's own files that the installer changes (GtEngine.ini, the sound dll), kept in the
  game folder (DeadhuntFX\original) the first time and put back on uninstall. }
procedure Keep(Name: String);
var
  Src, Dst: String;
begin
  Src := ExpandConstant('{app}\') + Name;
  Dst := ExpandConstant('{app}\DeadhuntFX\original\') + Name;
  if FileExists(Src) and not FileExists(Dst) then begin
    ForceDirectories(ExtractFileDir(Dst));
    FileCopy(Src, Dst, True);
  end;
end;

function ScanAngle(W, H: Integer): String;
begin
  { terrain culling wedge for a 90 degree FOV (FOVCoef 1.875), as DeadhuntFX computes it }
  if W * 10 <= H * 24 then Result := '66'
  else if W * 10 <= H * 36 then Result := '75'
  else Result := '80';
end;

var
  FirstInstall: Boolean;

procedure CurStepChanged(CurStep: TSetupStep);
var
  Ini: String;
  W, H: Integer;
begin
  if CurStep = ssInstall then begin
    { an earlier install already kept the game's GtEngine.ini: this is an update }
    FirstInstall := not FileExists(ExpandConstant('{app}\DeadhuntFX\original\GtEngine.ini'));
    Keep('GtEngine.ini');
    if WizardIsComponentSelected('openal') then begin
      Keep('OpenAL32.dll');
      Keep('wrap_oal.dll');
    end;
  end;
  { recommended game settings only on the first install: an update keeps what the player chose }
  if (CurStep = ssPostInstall) and FirstInstall then begin
    Ini := ExpandConstant('{app}\GtEngine.ini');
    W := GetSystemMetrics(0);
    H := GetSystemMetrics(1);
    SetIniString('GRAPHICS', 'FullScreen', '0', Ini);             { borderless window by DeadhuntFX }
    SetIniString('GRAPHICS', 'BackBufferWidth', IntToStr(W), Ini);
    SetIniString('GRAPHICS', 'BackBufferHeight', IntToStr(H), Ini);
    SetIniString('GRAPHICS', 'FOVCoef', '1.8750', Ini);
    SetIniString('GRAPHICS', 'ScanAngle', ScanAngle(W, H), Ini);
    SetIniString('GRAPHICS', 'LimitFPS', '1', Ini);
    SetIniString('GRAPHICS', 'MinFPS', '20', Ini);
    SetIniString('GRAPHICS', 'MaxFPS', '100000', Ini);
    SetIniString('GRAPHICS', 'GrassDensity', '4', Ini);
    SetIniString('GRAPHICS', 'GrassLevel', '3', Ini);
    SetIniString('GRAPHICS', 'EffectLevel', '3', Ini);
    SetIniString('GRAPHICS', 'ShadowLevel', '3', Ini);
    SetIniString('GRAPHICS', 'BloodLevel', '3', Ini);
    SetIniString('GAME', 'SmoothMouse', '0', Ini);
  end;
  if CurStep = ssPostInstall then begin
    { OpenAL Soft not wanted (any more): the game's own file back }
    if not WizardIsComponentSelected('openal') and FileExists(ExpandConstant('{app}\DeadhuntFX\original\OpenAL32.dll')) then
      FileCopy(ExpandConstant('{app}\DeadhuntFX\original\OpenAL32.dll'), ExpandConstant('{app}\OpenAL32.dll'), False);
  end;
end;

{ ---- uninstall: the kept game files go back before DeadhuntFX\ is removed }
procedure Restore(Name: String);
var
  Src: String;
begin
  Src := ExpandConstant('{app}\DeadhuntFX\original\') + Name;
  if FileExists(Src) then FileCopy(Src, ExpandConstant('{app}\') + Name, False);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then begin
    Restore('GtEngine.ini');
    Restore('OpenAL32.dll');
    Restore('wrap_oal.dll');
  end;
end;

