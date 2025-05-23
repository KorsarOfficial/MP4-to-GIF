# MP4 в GIF конвертер

Простая программа с графическим интерфейсом для конвертации MP4 видео в GIF-анимации.

## Требования

- Python 3.6 или выше
- FFmpeg (должен быть установлен и доступен в PATH)

## Установка FFmpeg

### Windows
1. Скачайте FFmpeg с официального сайта: https://ffmpeg.org/download.html#build-windows
2. Распакуйте архив
3. Добавьте путь к папке bin (например, C:\ffmpeg\bin) в переменную PATH

### macOS
```
brew install ffmpeg
```

### Linux
```
sudo apt install ffmpeg    # Debian/Ubuntu
sudo dnf install ffmpeg    # Fedora
```

## Использование

Запустите программу:

```
python mp4_to_gif_gui.py
```

### Функциональность

1. Нажмите кнопку "Выбрать видео" и выберите один или несколько MP4 файлов
2. Укажите желаемое количество кадров в секунду (FPS) в диапазоне от 1 до 60 (по умолчанию 10)
3. Нажмите "Конвертировать в GIF" для начала конвертации
4. После конвертации вы можете:
   - Сохранить все GIF файлы в ZIP архив, нажав "Сохранить в ZIP"
   - Выбрать файл из списка и нажать "Сохранить как" для сохранения его в другом месте

## Примечания

- Конвертация может занять время, особенно для больших видео
- Более низкий FPS поможет создать файлы меньшего размера, но с менее плавной анимацией
- Более высокий FPS дает более плавную анимацию, но увеличивает размер файла 