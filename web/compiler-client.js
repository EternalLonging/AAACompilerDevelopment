// 管理浏览器编译线程；超时或崩溃后，下次请求会创建新线程。
export class BrowserCompiler {
  constructor() {
    this.worker = null;
    this.starting = null;
    this.pending = null;
    this.sequence = 0;
  }

  initialize() {
    if (this.starting) return this.starting;
    this.starting = new Promise((resolve, reject) => {
      this.startupReject = reject;
      this.startupTimer = setTimeout(() => this.reset(new Error("编译器加载超时，请检查网络后重试")), 60000);
      this.worker = new Worker(new URL("./wasm-worker.js", import.meta.url), {type: "module"});
      this.worker.onerror = event => {
        event.preventDefault();
        this.reset(new Error("编译线程异常，请重试"));
      };
      this.worker.onmessage = ({data}) => {
        if (data.type === "ready") {
          clearTimeout(this.startupTimer);
          this.startupReject = null;
          resolve();
        } else if (data.type === "fatal") {
          this.reset(new Error(data.error));
        } else if (this.pending && data.id === this.pending.id) {
          const pending = this.pending;
          this.pending = null;
          clearTimeout(pending.timer);
          if (data.error) pending.reject(new Error(data.error));
          else pending.resolve(data.result);
        }
      };
    });
    return this.starting;
  }

  async request(action, payload) {
    await this.initialize();
    if (this.pending) throw new Error("正在编译，请稍后重试");
    return new Promise((resolve, reject) => {
      const id = ++this.sequence;
      const timer = setTimeout(() => this.reset(new Error("编译运行超过 15 秒，已停止；可以修改源码后重试")), 15000);
      this.pending = {id, resolve, reject, timer};
      this.worker.postMessage({id, action, payload});
    });
  }

  reset(error) {
    clearTimeout(this.startupTimer);
    this.worker?.terminate();
    this.startupReject?.(error);
    this.startupReject = null;
    if (this.pending) {
      clearTimeout(this.pending.timer);
      this.pending.reject(error);
    }
    this.pending = null;
    this.worker = null;
    this.starting = null;
  }
}
