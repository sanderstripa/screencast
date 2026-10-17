#pragma once
#include <string>
#include <map>
// UI-only translation; device names, filenames and OS error details remain verbatim.
inline std::wstring uiText(std::wstring value,bool english) {
    if(!english)return value;
    if(value.starts_with(L"Экран ")){
        value=L"Monitor "+value.substr(6);auto pos=value.find(L" · основной");if(pos!=std::wstring::npos)value.replace(pos,std::wstring(L" · основной").size(),L" · primary");return value;
    }
    static std::map<std::wstring,std::wstring> const words{
        {L"Настройки",L"Settings"},{L"Назад",L"Back"},{L"ТЕМА",L"THEME"},{L"ЯЗЫК",L"LANGUAGE"},
        {L"ПАПКА СОХРАНЕНИЯ",L"SAVE FOLDER"},{L"Выбрать папку…",L"Choose folder…"},{L"Система",L"System"},{L"Светлая",L"Light"},{L"Тёмная",L"Dark"},
        {L"Источник",L"Source"},{L"Запись экрана",L"Screen capture"},{L"Отдельное окно",L"Window capture"},{L"Обновить",L"Refresh"},
        {L"Экран / окно",L"Monitor / window"},{L"Выходной звук",L"Output audio"},{L"Звук выключен",L"Audio off"},{L"Системный звук",L"System audio"},
        {L"Звук приложения / процесса",L"Application / process audio"},{L"Устройство выхода",L"Output device"},{L"Микрофон",L"Microphone"},
        {L"Устройство микрофона",L"Microphone device"},{L"Громкость микрофона",L"Microphone volume"},{L"Шумоподавление",L"Noise suppression"},{L"Интенсивность",L"Intensity"},
        {L"Кружок на экране",L"Webcam overlay"},{L"Камера",L"Camera"},{L"Положение",L"Position"},{L"Слева сверху",L"Top left"},{L"Справа сверху",L"Top right"},
        {L"Слева снизу",L"Bottom left"},{L"Справа снизу",L"Bottom right"},{L"Зеркало",L"Mirror"},{L"Фон",L"Background"},{L"Реальный фон",L"Real background"},
        {L"Размыть реальный фон",L"Blur real background"},{L"Однотонный фон",L"Solid color"},{L"Спокойный градиент",L"Soft gradient"},{L"Нейтральный интерьер",L"Neutral interior"},
        {L"Своё изображение",L"Custom image"},{L"Вариант",L"Preset"},{L"Размытие",L"Blur"},{L"Выбрать своё изображение…",L"Choose custom image…"},
        {L"Молочная гостиная",L"Milk living room"},{L"Бежевый уголок",L"Beige corner"},{L"Светло-серый интерьер",L"Light grey interior"},
        {L"Графитовый",L"Graphite"},{L"Молочный",L"Milk"},{L"Светло-серый",L"Light grey"},{L"Тёплый бежевый",L"Warm beige"},
        {L"Размытие",L"Blur"},{L"Куда сохранить MP4…",L"Choose MP4 file…"},{L"Начать запись",L"Start recording"},{L"Остановить запись",L"Stop recording"},
        {L"Запись экрана · MP4",L"Screen recording · MP4"},{L"ИСТОЧНИК ВИДЕО",L"VIDEO SOURCE"},{L"ЗВУК",L"AUDIO"},{L"КАМЕРА И ФОН",L"CAMERA AND BACKGROUND"},
        {L"Перетащите кружок прямо на рабочем столе.",L"Drag the webcam circle on your desktop."},{L"Эффекты работают локально на компьютере.",L"Background effects run locally."},
        {L"Загрузка модели…",L"Downloading model…"},{L"ПРЕДПРОСМОТР",L"PREVIEW"},{L"Предпросмотр появится после начала записи",L"Preview appears when recording starts"},
        {L"Готов к записи",L"Ready to record"},{L"Нет доступных устройств",L"No devices available"},{L"По умолчанию",L"Default"},
        {L"Загружаю эффекты фона (15 МБ)…",L"Downloading background effects (15 MB)…"},{L"Эффекты фона готовы",L"Background effects ready"},
        {L"Загрузка не удалась. Нажмите «Загрузить эффекты» ещё раз",L"Download failed. Select Download effects to retry"},
        {L"Загрузка эффектов…",L"Downloading effects…"},{L"Загрузить эффекты фона (15 МБ)",L"Download background effects (15 MB)"},
        {L"Завершаю MP4…",L"Finalizing MP4…"},{L"Подключаю источники…",L"Connecting sources…"},{L"Запись остановлена",L"Recording stopped"},
        {L"Завершаю запись перед выходом…",L"Finishing recording before exit…"},{L"Сначала выберите своё изображение до начала записи.",L"Choose your custom image before recording starts."},
        {L"Нет доступного источника видео",L"No video source available"},{L"Нет доступных приложений для захвата звука",L"No applications available for audio capture"},
        {L"Камера не подключена",L"No camera connected"},{L"Выберите изображение фона",L"Choose a background image"},{L"Не удалось открыть диалог выбора файла",L"Could not open the file dialog"},
        {L"Запись сохранена · ",L"Recording saved · "},{L"Запись · ",L"Recording · "},{L" · аппаратное кодирование",L" · hardware encoding"},{L" · программное кодирование",L" · software encoding"},
        {L"Уведомление",L"Notice"},{L"Закрыть",L"Close"},{L"Папка используется для новых записей. Файл MP4 можно выбрать перед записью.",L"Used for new recordings. You can still choose an MP4 file before recording."}
    };
    // Longer entries first to keep compound messages intact.
    if(auto found=words.find(value);found!=words.end())return found->second;
    for(auto const& [ru,en]:words)if(value.starts_with(ru))return en+value.substr(ru.size());
    return value;
}
