import os
import zipfile
import threading
import tkinter as tk
import subprocess
import sys
from tkinter import filedialog, ttk, messagebox

class MP4toGIFConverter:
    def __init__(self, root):
        self.root = root
        self.root.title("MP4 в GIF Конвертер")
        self.root.geometry("600x500")
        self.root.resizable(True, True)
        
        self.selected_videos = []
        self.converted_gifs = []
        
        self.create_widgets()
        
    def create_widgets(self):
        main_frame = ttk.Frame(self.root, padding="10")
        main_frame.pack(fill=tk.BOTH, expand=True)
        
        btn_frame = ttk.Frame(main_frame)
        btn_frame.pack(fill=tk.X, pady=10)
        
        self.select_btn = ttk.Button(btn_frame, text="Выбрать видео", command=self.select_videos)
        self.select_btn.pack(side=tk.LEFT, padx=5)
        
        self.convert_btn = ttk.Button(btn_frame, text="Конвертировать в GIF", command=self.start_conversion)
        self.convert_btn.pack(side=tk.LEFT, padx=5)
        
        self.zip_btn = ttk.Button(btn_frame, text="Сохранить в ZIP", command=self.save_to_zip)
        self.zip_btn.pack(side=tk.LEFT, padx=5)
        self.zip_btn["state"] = "disabled"
        
        self.saveas_btn = ttk.Button(btn_frame, text="Сохранить как", command=self.save_as)
        self.saveas_btn.pack(side=tk.LEFT, padx=5)
        self.saveas_btn["state"] = "disabled"
        
        fps_frame = ttk.Frame(main_frame)
        fps_frame.pack(fill=tk.X, pady=5)
        
        ttk.Label(fps_frame, text="FPS (1-60):").pack(side=tk.LEFT, padx=5)
        
        self.fps_var = tk.StringVar(value="10")
        self.fps_entry = ttk.Entry(fps_frame, textvariable=self.fps_var, width=5)
        self.fps_entry.pack(side=tk.LEFT, padx=5)
        
        list_frame = ttk.LabelFrame(main_frame, text="Выбранные файлы")
        list_frame.pack(fill=tk.BOTH, expand=True, pady=10)
        
        self.file_listbox = tk.Listbox(list_frame)
        self.file_listbox.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)
        
        self.progress_var = tk.DoubleVar()
        self.progress = ttk.Progressbar(main_frame, variable=self.progress_var, maximum=100)
        self.progress.pack(fill=tk.X, pady=5)
        
        self.status_label = ttk.Label(main_frame, text="Готов к работе")
        self.status_label.pack(anchor=tk.W, pady=5)
    
    def select_videos(self):
        files = filedialog.askopenfilenames(
            title="Выберите MP4 файлы",
            filetypes=[("MP4 файлы", "*.mp4"), ("Все файлы", "*.*")]
        )
        if files:
            self.selected_videos = list(files)
            self.update_file_list()
    
    def update_file_list(self):
        self.file_listbox.delete(0, tk.END)
        for file in self.selected_videos:
            self.file_listbox.insert(tk.END, os.path.basename(file))
    
    def update_status(self, text):
        self.status_label.config(text=text)
        self.root.update_idletasks()
    
    def get_fps(self):
        try:
            fps = int(self.fps_var.get())
            if fps < 1:
                fps = 1
                self.fps_var.set("1")
            elif fps > 60:
                fps = 60
                self.fps_var.set("60")
            return fps
        except ValueError:
            self.fps_var.set("10")
            return 10
    
    def start_conversion(self):
        if not self.selected_videos:
            messagebox.showinfo("Информация", "Пожалуйста, выберите видео файлы.")
            return
        
        fps = self.get_fps()
        
        self.converted_gifs = []
        self.select_btn["state"] = "disabled"
        self.convert_btn["state"] = "disabled"
        self.zip_btn["state"] = "disabled"
        self.saveas_btn["state"] = "disabled"
        self.fps_entry["state"] = "disabled"
        
        thread = threading.Thread(target=self.convert_videos, args=(fps,))
        thread.daemon = True
        thread.start()
    
    def convert_videos(self, fps=10):
        total = len(self.selected_videos)
        
        for i, video_path in enumerate(self.selected_videos):
            try:
                self.update_status(f"Конвертация {i+1}/{total}: {os.path.basename(video_path)} с {fps} FPS")
                
                gif_path = os.path.splitext(video_path)[0] + ".gif"
                
                self.convert_mp4_to_gif(video_path, gif_path, fps)
                
                self.converted_gifs.append(gif_path)
                
                self.progress_var.set((i+1) / total * 100)
            except Exception as e:
                self.update_status(f"Ошибка при конвертации {os.path.basename(video_path)}: {str(e)}")
        
        self.update_status(f"Готово! Сконвертировано {len(self.converted_gifs)} из {total} файлов.")
        self.select_btn["state"] = "normal"
        self.convert_btn["state"] = "normal"
        self.fps_entry["state"] = "normal"
        
        if self.converted_gifs:
            self.zip_btn["state"] = "normal"
            self.saveas_btn["state"] = "normal"
    
    def convert_mp4_to_gif(self, input_path, output_path, fps=10):
        try:
            if sys.platform == "win32":
                ffmpeg_cmd = [
                    "ffmpeg", "-i", input_path, 
                    "-vf", f"fps={fps}", 
                    "-c:v", "gif", 
                    output_path
                ]
            else:
                ffmpeg_cmd = [
                    "ffmpeg", "-i", input_path, 
                    "-vf", f"fps={fps}", 
                    output_path
                ]
            
            process = subprocess.Popen(
                ffmpeg_cmd, 
                stdout=subprocess.PIPE, 
                stderr=subprocess.PIPE
            )
            
            stdout, stderr = process.communicate()
            
            if process.returncode != 0:
                raise Exception(f"FFmpeg error: {stderr.decode()}")
                
            return True
        except Exception as e:
            if "FFmpeg" in str(e):
                messagebox.showerror("Ошибка", "Для конвертации необходим FFmpeg. Пожалуйста, установите его: https://ffmpeg.org/download.html")
            raise e
    
    def save_to_zip(self):
        if not self.converted_gifs:
            return
        
        zip_path = filedialog.asksaveasfilename(
            title="Сохранить ZIP архив",
            defaultextension=".zip",
            filetypes=[("ZIP архивы", "*.zip")]
        )
        
        if zip_path:
            try:
                self.update_status("Создание ZIP архива...")
                with zipfile.ZipFile(zip_path, 'w') as zipf:
                    for gif in self.converted_gifs:
                        zipf.write(gif, os.path.basename(gif))
                
                self.update_status(f"ZIP архив сохранен: {zip_path}")
                messagebox.showinfo("Успех", f"ZIP архив сохранен: {zip_path}")
            except Exception as e:
                self.update_status(f"Ошибка при создании ZIP: {str(e)}")
                messagebox.showerror("Ошибка", f"Не удалось создать ZIP: {str(e)}")
    
    def save_as(self):
        if not self.converted_gifs:
            return
        
        selected_idx = self.file_listbox.curselection()
        if not selected_idx:
            messagebox.showinfo("Информация", "Пожалуйста, выберите файл из списка.")
            return
        
        try:
            idx = selected_idx[0]
            if idx < len(self.converted_gifs):
                original_gif = self.converted_gifs[idx]
                
                new_path = filedialog.asksaveasfilename(
                    title="Сохранить GIF как",
                    defaultextension=".gif",
                    filetypes=[("GIF файлы", "*.gif")]
                )
                
                if new_path:
                    if os.path.exists(original_gif):
                        with open(original_gif, 'rb') as src, open(new_path, 'wb') as dst:
                            dst.write(src.read())
                        self.update_status(f"GIF сохранен как: {new_path}")
                        messagebox.showinfo("Успех", f"GIF сохранен как: {new_path}")
                    else:
                        messagebox.showerror("Ошибка", "Исходный GIF файл не найден.")
        except Exception as e:
            self.update_status(f"Ошибка при сохранении: {str(e)}")
            messagebox.showerror("Ошибка", f"Не удалось сохранить файл: {str(e)}")

if __name__ == "__main__":
    root = tk.Tk()
    app = MP4toGIFConverter(root)
    root.mainloop() 