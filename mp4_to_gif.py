import os
import argparse
from moviepy.editor import VideoFileClip

def convert_mp4_to_gif(input_path, output_path=None, fps=10, resize_factor=1.0):
    if not os.path.exists(input_path):
        print(f"Ошибка: Файл {input_path} не найден.")
        return
    
    if output_path is None:
        output_path = os.path.splitext(input_path)[0] + ".gif"
    
    print(f"Конвертация {input_path} в {output_path}")
    print("Это может занять некоторое время в зависимости от размера видео...")
    
    try:
        video_clip = VideoFileClip(input_path)
        
        if resize_factor != 1.0:
            video_clip = video_clip.resize(resize_factor)
        
        video_clip.write_gif(output_path, fps=fps)
        
        print(f"Готово! GIF сохранен в {output_path}")
    except Exception as e:
        print(f"Произошла ошибка при конвертации: {e}")
    finally:
        if 'video_clip' in locals():
            video_clip.close()

def main():
    parser = argparse.ArgumentParser(description='Конвертирует MP4 видео в GIF.')
    parser.add_argument('input', help='Путь к MP4 файлу')
    parser.add_argument('-o', '--output', help='Путь для сохранения GIF (опционально)')
    parser.add_argument('-f', '--fps', type=int, default=10, help='Количество кадров в секунду (по умолчанию: 10)')
    parser.add_argument('-r', '--resize', type=float, default=1.0, help='Коэффициент изменения размера (по умолчанию: 1.0)')
    
    args = parser.parse_args()
    
    convert_mp4_to_gif(args.input, args.output, args.fps, args.resize)

if __name__ == "__main__":
    main() 