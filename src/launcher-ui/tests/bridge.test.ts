import assert from "node:assert/strict";
import test from "node:test";
import { NativeBridge, type NativeTransport } from "../src/bridge";

function transport() {
  const requests: Array<{ id: number; session: string; method: string }> = [];
  let listener: ((event: MessageEvent) => void) | undefined;
  const api: NativeTransport = {
    postMessage: (value) => requests.push(value as (typeof requests)[number]),
    addEventListener: (_, handler) => {
      listener = handler;
    },
    removeEventListener: () => {
      listener = undefined;
    },
  };
  return {
    api,
    requests,
    receive: (data: unknown) => listener?.({ data } as MessageEvent),
  };
}
test("replies from an old document cannot resolve a new document request", async () => {
  const mock = transport();
  const bridge = new NativeBridge(mock.api, "new-document");
  const response = bridge.request("bootstrap");
  mock.receive({
    id: mock.requests[0].id,
    session: "old-document",
    result: { ok: true, wrong: true },
  });
  mock.receive({
    id: mock.requests[0].id,
    session: "new-document",
    result: { ok: true },
  });
  assert.deepEqual(await response, { ok: true });
  bridge.dispose();
});
test("native errors and missing hosts reject without leaving requests pending", async () => {
  const mock = transport();
  const bridge = new NativeBridge(mock.api, "document");
  const response = bridge.request("settings.save");
  mock.receive({
    id: mock.requests[0].id,
    session: "document",
    result: { ok: false, errorKey: "error.disk" },
  });
  await assert.rejects(response, /error.disk/);
  await assert.rejects(
    new NativeBridge(undefined).request("bootstrap"),
    /launcher.hostRequired/,
  );
  bridge.dispose();
});
test("closing the document retires pending requests", async () => {
  const bridge = new NativeBridge(transport().api, "document");
  const pending = bridge.request("bootstrap");
  bridge.dispose();
  await assert.rejects(pending, /launcher.closed/);
});
test("window state changes are scoped to the current document and retire with the component", () => {
  const mock = transport();
  const bridge = new NativeBridge(mock.api, "new-document");
  const states: boolean[] = [];
  const unsubscribe = bridge.onWindowState((state) =>
    states.push(state.maximized),
  );
  mock.receive({
    session: "old-document",
    event: "window.state",
    state: { maximized: true },
  });
  mock.receive({
    session: "new-document",
    event: "window.state",
    state: { maximized: true },
  });
  unsubscribe();
  mock.receive({
    session: "new-document",
    event: "window.state",
    state: { maximized: false },
  });
  assert.deepEqual(states, [true]);
  bridge.dispose();
});
