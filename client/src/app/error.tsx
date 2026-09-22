"use client";

import { useEffect } from "react";

import { Button } from "@/components/ui/button";

export default function Error({
  error,
  reset,
}: {
  error: Error & { digest?: string };
  reset: () => void;
}) {
  useEffect(() => {
    console.error(error);
  }, [error]);

  return (
    <div className="mx-auto flex min-h-full w-full max-w-xl flex-1 flex-col justify-center px-4 py-16">
      <p className="font-mono text-xs tracking-[0.16em] text-muted-foreground uppercase">
        kChain
      </p>
      <h1 className="mt-2 font-display text-4xl">This page hit an error</h1>
      <p className="mt-3 text-sm leading-6 text-muted-foreground">
        {error.message || "Something broke while rendering the vault editor."}
      </p>
      <div className="mt-6">
        <Button type="button" onClick={() => reset()}>
          Try again
        </Button>
      </div>
    </div>
  );
}
