import createMiniC from "./minic.js";

let stdout = [], stderr = [];
const encoder = new TextEncoder();

// 检查项目文件并返回规范路径，规则与本地工作台一致。
function projectPath(raw) {
  if (typeof raw !== "string") throw new Error("文件名必须为文本");
  const name = raw.trim().replaceAll("\\", "/");
  if (name.length > 180 || !/^[\w\u0080-\uffff /.-]+\.c$/.test(name) || name.split("/").some(p =>
    !p || p === "." || p === ".." || /[. ]$/.test(p) || /^(con|prn|aux|nul|com[1-9]|lpt[1-9])(\.|$)/i.test(p))) {
    throw new Error("文件名应为项目内的 .c 相对路径");
  }
  return name;
}

function projectFiles(payload) {
  if (!payload || typeof payload !== "object" || Array.isArray(payload)) throw new Error("项目格式不正确");
  const {source = "", input = "", files = {}, entry = "main.c"} = payload;
  if (typeof source !== "string" || typeof input !== "string" || !files || typeof files !== "object" || Array.isArray(files)) {
    throw new Error("源码、输入或项目文件格式不正确");
  }
  if (Object.keys(files).length > 32 || encoder.encode(source).length > 262144 || encoder.encode(input).length > 65536 ||
      encoder.encode(JSON.stringify(payload)).length > 2097152) throw new Error("源码、输入或项目文件数量超过工作台限制");
  const name = projectPath(entry), project = new Map([[name, source]]);
  for (const [raw, text] of Object.entries(files)) {
    if (typeof text !== "string") throw new Error("项目文件内容必须为文本");
    const path = projectPath(raw), lower = path.toLowerCase();
    if ([...project.keys()].some(p => lower === p.toLowerCase() || lower.startsWith(p.toLowerCase() + "/") || p.toLowerCase().startsWith(lower + "/"))) {
      throw new Error("项目文件重名或目录冲突");
    }
    if (encoder.encode(text).length > 262144) throw new Error("项目文件过大");
    project.set(path, text);
  }
  return {entry: name, input, project};
}

// 删除上次请求的内存文件，返回空项目目录。
function clearProject(FS, path = "/project") {
  if (!FS.analyzePath(path).exists) return;
  for (const name of FS.readdir(path).filter(n => n !== "." && n !== "..")) {
    const child = path + "/" + name;
    if (FS.isDir(FS.stat(child).mode)) clearProject(FS, child);
    else FS.unlink(child);
  }
  FS.rmdir(path);
}

try {
  const compiler = await createMiniC({print: text => stdout.push(text), printErr: text => stderr.push(text)});
  self.onmessage = ({data: {id, action, payload}}) => {
    try {
      if (action !== "run" && action !== "inspect") throw new Error("未知工作台操作");
      const {entry, input, project} = projectFiles(payload);
      const FS = compiler.FS;
      clearProject(FS);
      FS.mkdir("/project");
      FS.writeFile("/project/input.txt", input);
      for (const [name, text] of project) {
        const path = "/project/" + name;
        FS.mkdirTree(path.slice(0, path.lastIndexOf("/")));
        FS.writeFile(path, text);
      }
      stdout = []; stderr = [];
      const status = compiler.callMain([action, "/project/" + entry, "/project/input.txt", "0", "/project"]);
      if (status) throw new Error(stderr.join("\n") || "编译服务失败");
      self.postMessage({id, result: JSON.parse(stdout.join("\n"))});
    } catch (error) {
      // 无效输入可以继续使用线程；运行时崩溃则重建编译器。
      if (error instanceof WebAssembly.RuntimeError) self.postMessage({type: "fatal", error: "编译器运行异常，请修改源码后重试"});
      else self.postMessage({id, error: error.message || "编译失败"});
    }
  };
  self.postMessage({type: "ready"});
} catch (error) {
  self.postMessage({type: "fatal", error: "无法加载编译器，请刷新重试：" + error.message});
}
