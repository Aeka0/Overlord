export interface NativeTransport {
  postMessage(message: unknown): void;
  addEventListener(
    type: "message",
    handler: (event: MessageEvent) => void,
  ): void;
  removeEventListener(
    type: "message",
    handler: (event: MessageEvent) => void,
  ): void;
}
declare global {
  interface Window {
    chrome?: { webview?: NativeTransport };
  }
}

export class NativeBridge {
  private nextId = 0;
  private readonly windowListeners = new Set<
    (state: { maximized: boolean }) => void
  >();
  private readonly pending = new Map<
    number,
    {
      resolve: (result: unknown) => void;
      reject: (error: Error) => void;
      timer: ReturnType<typeof setTimeout>;
    }
  >();
  constructor(
    private readonly transport: NativeTransport | undefined,
    readonly session = crypto.randomUUID(),
    private readonly timeout = 30000,
  ) {
    transport?.addEventListener("message", this.receive);
  }
  private receive = (event: MessageEvent) => {
    const message = event.data;
    if (!message || message.session !== this.session) return;
    if (
      message.event === "window.state" &&
      typeof message.state?.maximized === "boolean"
    ) {
      for (const listener of this.windowListeners) listener(message.state);
      return;
    }
    if (!Number.isInteger(message.id)) return;
    const pending = this.pending.get(message.id);
    if (!pending) return;
    this.pending.delete(message.id);
    clearTimeout(pending.timer);
    if (message.result?.ok === false)
      pending.reject(
        new Error(
          message.result.errorKey || message.result.error || "error.saveVR",
        ),
      );
    else if (message.result && typeof message.result === "object")
      pending.resolve(message.result);
    else pending.reject(new Error("error.invalidVR"));
  };
  request<T>(method: string, params: Record<string, unknown> = {}): Promise<T> {
    if (!this.transport)
      return Promise.reject(new Error("launcher.hostRequired"));
    if (this.pending.size >= 64)
      return Promise.reject(new Error("launcher.busy"));
    const id = ++this.nextId;
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pending.delete(id);
        reject(new Error("launcher.timeout"));
      }, this.timeout);
      this.pending.set(id, {
        resolve: (result) => resolve(result as T),
        reject,
        timer,
      });
      try {
        this.transport!.postMessage({
          id,
          session: this.session,
          method,
          params,
        });
      } catch (error) {
        this.pending.delete(id);
        clearTimeout(timer);
        reject(
          error instanceof Error ? error : new Error("launcher.hostRequired"),
        );
      }
    });
  }
  dispose() {
    this.transport?.removeEventListener("message", this.receive);
    for (const pending of this.pending.values()) {
      clearTimeout(pending.timer);
      pending.reject(new Error("launcher.closed"));
    }
    this.pending.clear();
    this.windowListeners.clear();
  }
  onWindowState(listener: (state: { maximized: boolean }) => void) {
    this.windowListeners.add(listener);
    return () => {
      this.windowListeners.delete(listener);
    };
  }
}
export const bridge =
  typeof window !== "undefined"
    ? new NativeBridge(window.chrome?.webview)
    : undefined;
if (import.meta.hot) import.meta.hot.dispose(() => bridge?.dispose());
