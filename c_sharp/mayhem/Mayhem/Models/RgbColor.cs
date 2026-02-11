namespace Mayhem.Models;

public struct RgbColor
{
    public byte R { get; set; }
    public byte G { get; set; }
    public byte B { get; set; }

    public RgbColor(byte r, byte g, byte b)
    {
        R = r;
        G = g;
        B = b;
    }

    public static RgbColor Red => new(255, 0, 0);
    public static RgbColor Yellow => new(255, 255, 0);
    public static RgbColor LightGreen => new(144, 238, 144);
    public static RgbColor LightBlue => new(173, 216, 230);
    public static RgbColor Pink => new(255, 192, 203);


    public string ToJson() => System.Text.Json.JsonSerializer.Serialize(this, JsonUtil.Options);

    public static RgbColor FromJson(string json) =>
        System.Text.Json.JsonSerializer.Deserialize<RgbColor>(json, JsonUtil.Options);
}
