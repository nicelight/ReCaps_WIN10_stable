Runbook: сборка Recaps Switcher
===============================

## Предпосылки

1. Windows 10/11 x64.
2. Установленные Build Tools for Visual Studio 2026 **или** 2022 (подойдёт и Community-редакция).  
   В workload «Разработка классических приложений на C++» включите:
   - MSVC toolset (**v145** для 2026 или **v143** для 2022);
   - Windows 10/11 SDK.
   MFC не требуется: ресурсы используют `winres.h`, а не `afxres.h`.
3. PowerShell или cmd с правами пользователя.

## Окно разработчика

1. Откройте «x64 Native Tools Command Prompt for VS» соответствующей версии.
2. Если используете общее cmd/PowerShell, инициализируйте окружение вручную:
   ```
   call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
   ```
   (для VS2022 путь будет `...\2022\BuildTools\...`).

## Команда сборки

```
cd /d <папка-репозитория>
msbuild recaps.2010.vcxproj ^
  /p:Configuration=Release ^
  /p:Platform=x64 ^
  /p:PlatformToolset=v145 ^
  /p:PreBuildEventUseInBuild=false
```

*Примечания:*
- если установлен toolset v143, замените `/p:PlatformToolset=v145` на `v143`;
- prebuild-скрипт `build-release.wsf` отключён, поэтому версия не инкрементируется автоматически;
- результат (`recaps.exe`) появится в `x64\Release\`;
- для 32-битной сборки используйте `/p:Platform=Win32` (результат в `Win32\Release\`);
- локализация: строки — в `resource.rc` и `lang_ru.rc2`, выбор языка — `localization.cpp`.

## CI и релизы

`.github/workflows/release.yml` собирает x64 и Win32 на каждый push/PR.
При пуше тега вида `v*` создаётся GitHub Release с архивами
`recaps-x64.zip` и `recaps-Win32.zip` (внутри — `recaps.exe`, `help`, `flags`).

## Типичные ошибки

| Сообщение                                 | Решение |
|-------------------------------------------|---------|
| `MSB8020: The build tools for ... cannot be found` | Проверьте `/p:PlatformToolset` и установку соответствующего MSVC (см. «Предпосылки»). |
| `msbuild is not recognized ...`           | Запускайте команду из «Native Tools Command Prompt» либо импортируйте `vcvars64.bat`. |
| `RC1015: cannot open include file 'winres.h'` | Установите Windows 10/11 SDK (заголовки ресурсов). |

## Проверка

1. Запустите `x64\Release\recaps.exe`.
2. Проверьте:
   - переключение раскладок по CapsLock;
   - `Ctrl+CapsLock` (конвертация текста в другой раскладке);
   - окно настроек (Settings → Help открывается, изменения сохраняются);
   - переключение языка: трей → «Язык» / «Language» → English/Русский/Системный;
     после переключения откройте настройки и справку — они должны быть на выбранном языке.
