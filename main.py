import os
import re
import sys
import subprocess
import tkinter as tk
from tkinter import ttk, filedialog, messagebox
from pathlib import Path
from datetime import datetime

try:
    import psutil
except ImportError as exc:
    raise SystemExit("Missing dependency: run  py -m pip install psutil") from exc


APP_TITLE = "OnyxiaEXE"
BG = "#dce9f5"
PANEL = "#edf4fa"
PANEL_DARK = "#c7d9e9"
BORDER = "#7f9db9"
BLUE_DARK = "#173b63"
BLUE_MID = "#3979ae"
BLUE_LIGHT = "#d8edff"
TEXT = "#17324d"
MUTED = "#5d748b"
GREEN = "#2b8b57"
RED = "#a83b3b"
EDITOR_BG = "#fbfdff"
EDITOR_FG = "#172b3d"

STARTER = """-- OnyxiaEXE | Roblox Studio Luau workspace
-- Write scripts for experiences you create or edit in Roblox Studio.

local Players = game:GetService("Players")

print("Hello from Roblox Studio!")

-- Example for a LocalScript:
-- local player = Players.LocalPlayer
-- print(player and player.Name)
"""


class EditorTab:
    def __init__(self, app, name, source=""):
        self.app = app
        self.name = name
        self.path = None
        self.dirty = False

        self.frame = ttk.Frame(app.notebook)
        shell = tk.Frame(self.frame, bg=EDITOR_BG, highlightbackground=BORDER,
                         highlightthickness=1)
        shell.pack(fill="both", expand=True)

        self.text = tk.Text(
            shell, wrap="none", undo=True, maxundo=-1,
            bg=EDITOR_BG, fg=EDITOR_FG, insertbackground="#102a43",
            selectbackground="#c7e4ff", relief="flat", bd=0,
            padx=12, pady=12, font=("Consolas", 10),
            tabs=("4c",), highlightthickness=0
        )
        ybar = ttk.Scrollbar(shell, orient="vertical", command=self.text.yview)
        xbar = ttk.Scrollbar(shell, orient="horizontal", command=self.text.xview)
        self.text.configure(yscrollcommand=ybar.set, xscrollcommand=xbar.set)
        self.text.grid(row=0, column=0, sticky="nsew")
        ybar.grid(row=0, column=1, sticky="ns")
        xbar.grid(row=1, column=0, sticky="ew")
        shell.rowconfigure(0, weight=1)
        shell.columnconfigure(0, weight=1)

        self.text.tag_configure("keyword", foreground="#7b3e9d")
        self.text.tag_configure("string", foreground="#317a43")
        self.text.tag_configure("comment", foreground="#8192a3")
        self.text.tag_configure("number", foreground="#b45d27")
        self.text.tag_configure("builtin", foreground="#245d9b")
        self.text.insert("1.0", source)
        self.text.bind("<KeyRelease>", self.on_key)
        self.text.bind("<Control-s>", lambda _e: (app.save_current(), "break"))
        self.text.bind("<Control-Return>", lambda _e: (app.copy_current(), "break"))
        self.highlight()

    def on_key(self, _event=None):
        self.dirty = True
        self.app.update_tab_title(self)
        self.app.after_idle(self.highlight)

    def highlight(self):
        source = self.text.get("1.0", "end-1c")
        for tag in ("keyword", "string", "comment", "number", "builtin"):
            self.text.tag_remove(tag, "1.0", "end")

        patterns = [
            ("comment", r"--[^\n]*"),
            ("string", r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\''),
            ("number", r"\b\d+(?:\.\d+)?\b"),
            ("keyword", r"\b(?:and|break|do|else|elseif|end|false|for|function|if|in|local|nil|not|or|repeat|return|then|true|until|while|continue|export|type)\b"),
            ("builtin", r"\b(?:print|warn|require|game|workspace|script|Enum|Instance|Vector3|Vector2|CFrame|Color3|UDim2|UDim|task|math|string|table|pairs|ipairs|pcall|tostring|tonumber|typeof)\b")
        ]
        for tag, pattern in patterns:
            for match in re.finditer(pattern, source):
                start = f"1.0 + {match.start()} chars"
                end = f"1.0 + {match.end()} chars"
                self.text.tag_add(tag, start, end)


class OnyxiaApp(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title(APP_TITLE)
        self.geometry("1120x760")
        self.minsize(900, 620)
        self.configure(bg=BG)

        self.tabs = []
        self.active = None
        self.process_rows = {}
        self.selected_pid = None
        self.previous_pids = set()
        self._build_styles()
        self._build_menu()
        self._build_ui()
        self.new_tab("Starter.client.lua", STARTER)
        self.scan_processes(initial=True)
        self.after(4000, self._scheduled_scan)
        self.protocol("WM_DELETE_WINDOW", self.on_close)

    def _build_styles(self):
        style = ttk.Style(self)
        try:
            style.theme_use("vista")
        except tk.TclError:
            try:
                style.theme_use("clam")
            except tk.TclError:
                pass
        style.configure("Treeview", rowheight=25, font=("Segoe UI", 9),
                        background="#ffffff", fieldbackground="#ffffff",
                        foreground=TEXT)
        style.configure("Treeview.Heading", font=("Segoe UI", 9, "bold"),
                        background="#d8e7f4", foreground=TEXT)
        style.map("Treeview", background=[("selected", "#c5e2fa")],
                  foreground=[("selected", TEXT)])
        style.configure("TNotebook", background=BG, borderwidth=0)
        style.configure("TNotebook.Tab", padding=(12, 6), font=("Segoe UI", 9))

    def _build_menu(self):
        menu = tk.Menu(self)
        file_menu = tk.Menu(menu, tearoff=False)
        file_menu.add_command(label="New script", accelerator="Ctrl+N", command=self.new_tab)
        file_menu.add_command(label="Open...", accelerator="Ctrl+O", command=self.open_file)
        file_menu.add_command(label="Save", accelerator="Ctrl+S", command=self.save_current)
        file_menu.add_command(label="Save as...", command=self.save_as)
        file_menu.add_separator()
        file_menu.add_command(label="Exit", command=self.on_close)
        menu.add_cascade(label="File", menu=file_menu)

        tools_menu = tk.Menu(menu, tearoff=False)
        tools_menu.add_command(label="Scan Roblox processes", command=self.scan_processes)
        tools_menu.add_command(label="Copy script to clipboard", command=self.copy_current)
        tools_menu.add_command(label="Open Roblox folder", command=self.open_roblox_folder)
        tools_menu.add_command(label="Open Roblox Studio", command=self.open_studio)
        menu.add_cascade(label="Tools", menu=tools_menu)

        help_menu = tk.Menu(menu, tearoff=False)
        help_menu.add_command(label="About OnyxiaEXE", command=self.about)
        menu.add_cascade(label="Help", menu=help_menu)
        self.config(menu=menu)

        self.bind_all("<Control-n>", lambda _e: self.new_tab())
        self.bind_all("<Control-o>", lambda _e: self.open_file())
        self.bind_all("<Control-s>", lambda _e: self.save_current())

    def _gradient_header(self, parent, height=76):
        canvas = tk.Canvas(parent, height=height, bd=0, highlightthickness=0)
        canvas.pack(fill="x")
        top = (20, 62, 101)
        bottom = (72, 132, 183)
        for y in range(height):
            ratio = y / max(1, height - 1)
            color = tuple(int(top[i] * (1-ratio) + bottom[i] * ratio) for i in range(3))
            canvas.create_line(0, y, 2000, y, fill="#%02x%02x%02x" % color)
        canvas.create_text(22, 27, anchor="w", text="Onyxia",
                           fill="#ffffff", font=("Segoe UI", 22, "bold"))
        canvas.create_text(24, 55, anchor="w", text="ROBLOX STUDIO WORKBENCH",
                           fill="#d9edff", font=("Segoe UI", 8, "bold"))
        self.header_status = canvas.create_text(1085, 38, anchor="e",
                           text="● SEARCHING", fill="#e7f4ff",
                           font=("Segoe UI", 9, "bold"))
        return canvas

    def _button(self, parent, text, command, primary=False, width=None):
        return tk.Button(
            parent, text=text, command=command, width=width,
            bg="#c9deef" if not primary else "#b6d8f2",
            fg=TEXT, activebackground="#eaf5ff", activeforeground=TEXT,
            relief="raised", bd=1, padx=10, pady=4,
            font=("Segoe UI", 9, "bold"), cursor="hand2",
            highlightthickness=1, highlightbackground="#f8fcff"
        )

    def _build_ui(self):
        self._gradient_header(self)

        toolbar = tk.Frame(self, bg="#e7f0f8", bd=1, relief="raised")
        toolbar.pack(fill="x", padx=10, pady=(8, 7))
        buttons = [
            ("New", self.new_tab, False), ("Open", self.open_file, False),
            ("Save", self.save_current, False), ("Save As", self.save_as, False),
            ("Export .lua", self.export_script, False),
            ("Copy to Studio", self.copy_current, True),
            ("Scan Roblox", self.scan_processes, False),
            ("Open Studio", self.open_studio, False)
        ]
        for label, command, primary in buttons:
            self._button(toolbar, label, command, primary).pack(side="left", padx=4, pady=5)

        main = tk.Frame(self, bg=BG)
        main.pack(fill="both", expand=True, padx=10, pady=(0, 8))

        left = tk.Frame(main, bg=PANEL, bd=1, relief="groove", width=310)
        left.pack(side="left", fill="y", padx=(0, 8))
        left.pack_propagate(False)
        tk.Label(left, text="RUNNING ROBLOX PROCESSES", bg="#d8e7f4",
                 fg=TEXT, anchor="w", font=("Segoe UI", 9, "bold"),
                 padx=9, pady=7).pack(fill="x")
        tk.Label(left, text="Matches process name, executable path and command line.",
                 bg=PANEL, fg=MUTED, justify="left", wraplength=280,
                 font=("Segoe UI", 8)).pack(fill="x", padx=9, pady=(8, 4))

        columns = ("pid", "name", "memory")
        self.process_tree = ttk.Treeview(left, columns=columns, show="headings", height=12)
        self.process_tree.heading("pid", text="PID")
        self.process_tree.heading("name", text="PROCESS")
        self.process_tree.heading("memory", text="RAM")
        self.process_tree.column("pid", width=55, anchor="center")
        self.process_tree.column("name", width=155)
        self.process_tree.column("memory", width=65, anchor="e")
        self.process_tree.pack(fill="both", expand=True, padx=8, pady=5)
        self.process_tree.bind("<<TreeviewSelect>>", self.on_process_select)

        self.process_info = tk.Text(left, height=7, wrap="word", bg="#f8fbff",
                                    fg=TEXT, font=("Consolas", 8), relief="sunken",
                                    bd=1, padx=7, pady=6)
        self.process_info.pack(fill="x", padx=8, pady=(3, 8))
        self.process_info.insert("1.0", "Select a Roblox process to inspect its PID and path.")
        self.process_info.configure(state="disabled")

        right = tk.Frame(main, bg=BG)
        right.pack(side="left", fill="both", expand=True)
        editor_head = tk.Frame(right, bg="#d8e7f4", bd=1, relief="groove")
        editor_head.pack(fill="x")
        tk.Label(editor_head, text="SCRIPT EDITOR", bg="#d8e7f4", fg=TEXT,
                 font=("Segoe UI", 9, "bold"), padx=8, pady=6).pack(side="left")
        tk.Label(editor_head, text="Luau / Lua", bg="#d8e7f4", fg=MUTED,
                 font=("Segoe UI", 8)).pack(side="right", padx=9)

        self.notebook = ttk.Notebook(right)
        self.notebook.pack(fill="both", expand=True, pady=(0, 7))
        self.notebook.bind("<<NotebookTabChanged>>", self.on_tab_changed)

        output_frame = tk.Frame(right, bg=PANEL, bd=1, relief="groove")
        output_frame.pack(fill="x")
        tk.Label(output_frame, text="OUTPUT", bg="#d8e7f4", fg=TEXT,
                 anchor="w", font=("Segoe UI", 8, "bold"), padx=8, pady=4).pack(fill="x")
        self.output = tk.Text(output_frame, height=5, bg="#f9fcff", fg="#29435a",
                              font=("Consolas", 8), relief="sunken", bd=1,
                              padx=7, pady=5, wrap="word")
        self.output.pack(fill="x", padx=6, pady=6)
        self.log("OnyxiaEXE ready.")
        self.log("This app edits and exports Luau source for Roblox Studio.")
        self.log("Process inspection is read-only; it does not attach code to the game client.")

        self.statusbar = tk.Label(self, text="Ready", anchor="w", bg="#e4edf5",
                                  fg=MUTED, bd=1, relief="sunken",
                                  font=("Segoe UI", 8), padx=8, pady=3)
        self.statusbar.pack(fill="x", side="bottom")

    def log(self, message):
        stamp = datetime.now().strftime("%H:%M:%S")
        self.output.insert("end", f"[{stamp}] {message}\n")
        self.output.see("end")

    def set_process_status(self, count):
        state = f"● {count} ROBLOX PROCESS(ES)" if count else "● ROBLOX NOT FOUND"
        color = "#d8ffe5" if count else "#ffe8c7"
        self.header.itemconfigure(self.header_status, text=state, fill=color)
        self.statusbar.configure(text=state)

    def scan_processes(self, initial=False):
        found = {}
        for proc in psutil.process_iter(["pid", "name", "exe", "cmdline", "memory_info"]):
            try:
                name = proc.info.get("name") or ""
                exe = proc.info.get("exe") or ""
                command = " ".join(proc.info.get("cmdline") or [])
                if "roblox" not in f"{name} {exe} {command}".lower():
                    continue
                memory = proc.info.get("memory_info")
                found[proc.info["pid"]] = {
                    "name": name or "(unknown process)",
                    "exe": exe,
                    "cmdline": command,
                    "memory": memory.rss / (1024 * 1024) if memory else 0.0
                }
            except (psutil.NoSuchProcess, psutil.AccessDenied, psutil.ZombieProcess, KeyError):
                continue

        self.process_rows = found
        for item in self.process_tree.get_children():
            self.process_tree.delete(item)
        for pid, details in sorted(found.items()):
            self.process_tree.insert("", "end", iid=str(pid), values=(
                pid, details["name"], f"{details['memory']:.0f} MB"
            ))

        self.set_process_status(len(found))
        new_pids = set(found) - self.previous_pids
        if new_pids and not initial:
            self.log("Roblox process detected: PID " + ", ".join(map(str, sorted(new_pids))))
        if not found and not initial:
            self.log("No process containing 'Roblox' was found.")
        self.previous_pids = set(found)

        if self.selected_pid not in found:
            self.selected_pid = None
            self._set_process_info("Selected process is no longer running.")
        elif self.selected_pid is not None:
            self._show_process_info(self.selected_pid)

    def _scheduled_scan(self):
        self.scan_processes()
        self.after(4000, self._scheduled_scan)

    def on_process_select(self, _event=None):
        selection = self.process_tree.selection()
        if not selection:
            return
        try:
            self.selected_pid = int(selection[0])
        except ValueError:
            self.selected_pid = None
            return
        self._show_process_info(self.selected_pid)

    def _set_process_info(self, value):
        self.process_info.configure(state="normal")
        self.process_info.delete("1.0", "end")
        self.process_info.insert("1.0", value)
        self.process_info.configure(state="disabled")

    def _show_process_info(self, pid):
        details = self.process_rows.get(pid)
        if not details:
            return
        text = (
            f"PID: {pid}\n"
            f"Name: {details['name']}\n"
            f"RAM: {details['memory']:.1f} MB\n"
            f"Path: {details['exe'] or 'Unavailable (access denied)'}"
        )
        self._set_process_info(text)

    def new_tab(self, name=None, source=""):
        if callable(name):
            name = None
        name = name or f"Script{len(self.tabs) + 1}.client.lua"
        tab = EditorTab(self, name, source)
        self.tabs.append(tab)
        self.notebook.add(tab.frame, text=name)
        self.notebook.select(tab.frame)
        self.active = tab
        self.statusbar.configure(text=f"Created {name}")

    def on_tab_changed(self, _event=None):
        try:
            selected = self.notebook.select()
            self.active = next((t for t in self.tabs if str(t.frame) == selected), self.active)
        except tk.TclError:
            pass

    def update_tab_title(self, tab):
        suffix = "*" if tab.dirty else ""
        try:
            self.notebook.tab(tab.frame, text=tab.name + suffix)
        except tk.TclError:
            pass

    def current_source(self):
        if not self.active:
            return ""
        return self.active.text.get("1.0", "end-1c")

    def open_file(self):
        path = filedialog.askopenfilename(
            filetypes=[("Lua / Luau scripts", "*.lua"), ("Text files", "*.txt"), ("All files", "*.*")]
        )
        if not path:
            return
        try:
            content = Path(path).read_text(encoding="utf-8")
            self.new_tab(Path(path).name, content)
            self.active.path = Path(path)
            self.active.dirty = False
            self.update_tab_title(self.active)
            self.log(f"Opened {path}")
        except (OSError, UnicodeError) as exc:
            messagebox.showerror("Open failed", str(exc))

    def save_current(self):
        if not self.active:
            return
        if self.active.path is None:
            self.save_as()
            return
        self._write_active(self.active.path)

    def save_as(self):
        if not self.active:
            return
        path = filedialog.asksaveasfilename(
            defaultextension=".lua",
            initialfile=self.active.name,
            filetypes=[("Lua / Luau scripts", "*.lua"), ("All files", "*.*")]
        )
        if path:
            self.active.path = Path(path)
            self._write_active(self.active.path)

    def _write_active(self, path):
        try:
            Path(path).write_text(self.current_source(), encoding="utf-8")
            self.active.name = Path(path).name
            self.active.dirty = False
            self.update_tab_title(self.active)
            self.log(f"Saved {path}")
            self.statusbar.configure(text=f"Saved {path}")
        except OSError as exc:
            messagebox.showerror("Save failed", str(exc))

    def export_script(self):
        if not self.active:
            return
        path = filedialog.asksaveasfilename(
            defaultextension=".lua",
            initialfile=self.active.name,
            filetypes=[("Luau source", "*.lua")]
        )
        if path:
            try:
                Path(path).write_text(self.current_source(), encoding="utf-8")
                self.log(f"Exported Studio script: {path}")
                self.statusbar.configure(text="Script exported for Roblox Studio")
            except OSError as exc:
                messagebox.showerror("Export failed", str(exc))

    def copy_current(self):
        source = self.current_source()
        if not source:
            return
        self.clipboard_clear()
        self.clipboard_append(source)
        self.log("Current script copied. Paste it into a Script or LocalScript in Roblox Studio.")
        self.statusbar.configure(text="Script copied to clipboard")

    def open_roblox_folder(self):
        path = Path(os.path.expandvars(r"%LOCALAPPDATA%\Roblox"))
        if path.is_dir():
            os.startfile(str(path))
        else:
            messagebox.showinfo("Roblox folder", "Roblox folder was not found.")

    def open_studio(self):
        roots = [
            Path(os.path.expandvars(r"%LOCALAPPDATA%\Roblox\Versions")),
            Path(os.path.expandvars(r"%ProgramFiles(x86)%\Roblox\Versions"))
        ]
        for root in roots:
            if root.is_dir():
                matches = list(root.glob("*/RobloxStudioBeta.exe"))
                if matches:
                    try:
                        os.startfile(str(matches[-1]))
                        self.log("Roblox Studio launched.")
                        return
                    except OSError as exc:
                        messagebox.showerror("Launch failed", str(exc))
                        return
        messagebox.showinfo("Roblox Studio", "Roblox Studio was not found in its usual install folder.")

    def about(self):
        messagebox.showinfo(
            "About OnyxiaEXE",
            "OnyxiaEXE — Windows 7 Aero-style Roblox Studio workbench.\n"
            "Includes a read-only Roblox process finder and a Luau source editor."
        )

    def on_close(self):
        unsaved = [tab.name for tab in self.tabs if tab.dirty]
        if unsaved and not messagebox.askyesno(
            "Unsaved scripts",
            "These tabs have unsaved changes:\n\n" + "\n".join(unsaved) + "\n\nClose anyway?"
        ):
            return
        self.destroy()


if __name__ == "__main__":
    app = OnyxiaApp()
    app.mainloop()
