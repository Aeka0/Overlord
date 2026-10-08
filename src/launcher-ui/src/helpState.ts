export interface HelpState {
  category: string;
  query: string;
  storyAccepted: boolean;
  expanded: string;
}
export const initialHelpState: HelpState = {
  category: "movement",
  query: "",
  storyAccepted: false,
  expanded: "controls",
};
