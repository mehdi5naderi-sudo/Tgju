export default async function handler(req, res) {
  res.setHeader("Access-Control-Allow-Origin", "*");
  res.setHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
  res.setHeader("Access-Control-Allow-Headers", "Content-Type");
  res.setHeader("Cache-Control", "no-store, no-cache, must-revalidate");

  if (req.method === "OPTIONS") {
    return res.status(204).end();
  }

  if (req.method !== "GET") {
    res.setHeader("Allow", "GET, OPTIONS");
    return res.status(405).json({ error: "Method not allowed" });
  }

  try {
    const upstream = await fetch("https://call1.tgju.org/ajax.json", {
      method: "GET",
      headers: { Accept: "application/json" },
      cache: "no-store"
    });

    const body = await upstream.text();
    res.status(upstream.status);
    res.setHeader(
      "Content-Type",
      upstream.headers.get("content-type") || "application/json; charset=utf-8"
    );
    return res.send(body);
  } catch (error) {
    console.error("TGJU upstream error:", error);
    return res.status(502).json({
      error: "TGJU upstream fetch failed",
      message: String(error)
    });
  }
}