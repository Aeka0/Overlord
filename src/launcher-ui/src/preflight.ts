export interface PreflightIssue {
  id: string;
  severity: "error" | "warning";
  titleKey: string;
  detailKey: string;
  values?: Record<string, string | number>;
  fixable: boolean;
}

export interface PreflightReport {
  ok: true;
  issues: PreflightIssue[];
  canLaunch: boolean;
  gameAvailable: boolean;
}

export interface LaunchResult {
  ok: true;
  launched: boolean;
  preflight?: PreflightReport;
}
