declare module "*.mjs" {
  interface Session { execute(command: string): string; delete(): void }
  interface Physics { BrowserSession: new () => Session }
  export default function createPhysics(options: { locateFile(path: string): string }): Promise<Physics>;
}
