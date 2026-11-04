import { collect, type Draft, type SettingsState, type Values } from "./model";

// Physical hands and pose-pipeline banks are independent of weapon ownership.
export function calibrationBank(draft: Draft) {
  const standard = draft.vr_controllerPoseMode === "standard";
  const prefix = standard ? "vr_standardHand" : "vr_hand";
  const axes = ["Pitch", "Yaw", "Roll"];
  return {
    independent: `${prefix}AnglesIndependent`,
    initialized: `${prefix}AnglesInitialized`,
    shared: axes.map((axis) => `${prefix}Angle${axis}`),
    hands: ["Left", "Right"].map((hand) => axes.map((axis) =>
      `${standard ? `vr_standard${hand}` : `vr_${hand.toLowerCase()}`}HandAngle${axis}`)),
  };
}

export function angleModePatch(draft: Draft, state: SettingsState, enabled: boolean): Values {
  const bank = calibrationBank(draft);
  const patch: Values = { [bank.independent]: enabled };
  if (enabled && !draft[bank.initialized]) {
    const values = collect(draft, state, state.values, bank.shared);
    for (const hand of bank.hands)
      hand.forEach((key, axis) => { patch[key] = values[bank.shared[axis]]; });
    patch[bank.initialized] = true;
  } else if (draft[bank.independent]) {
    // Console-created independent profiles also retain their values on return.
    patch[bank.initialized] = true;
  }
  return patch;
}

export function handAnglePatch(draft: Draft, state: SettingsState,
  hand: "left" | "right", action: "reset" | "mirror"): Values {
  const bank = calibrationBank(draft);
  const destination = hand === "left" ? 0 : 1;
  const source = bank.hands[1 - destination];
  const values = action === "mirror" ? collect(draft, state, state.values, source) : {};
  return Object.fromEntries([
    [bank.initialized, true],
    ...bank.hands[destination].map((key, axis) => [key,
      action === "reset" ? 0 : Number(values[source[axis]]) * (axis === 0 ? 1 : -1)]),
  ]);
}
