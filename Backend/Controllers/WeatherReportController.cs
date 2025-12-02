using Microsoft.AspNetCore.Mvc;
using System.Text;
using System.Text.Json;

[Route("[controller]")]
public class WeatherReportController : Controller
{
    private readonly ApplicationDbContext _context;
    private readonly ILogger<WeatherReportController> _logger;

    public WeatherReportController(ApplicationDbContext context, ILogger<WeatherReportController> logger)
    {
        _context = context;
        _logger = logger;
    }

    [HttpPost]
    [Route("[action]")]
    public async Task<IActionResult> ReceiveData()
    {
        string body;
        using (StreamReader reader = new StreamReader(Request.Body, Encoding.UTF8))
        {
            body = await reader.ReadToEndAsync();
            _logger.LogInformation($"Raw body: {body}");
        }

        var dataList = JsonSerializer.Deserialize<List<WeatherData>>(body, new JsonSerializerOptions { PropertyNameCaseInsensitive = true });

        if (dataList is null || !dataList.Any())
        {
            _logger.LogWarning("Received null or empty data list.");
            return BadRequest("Data cannot be null or empty.");
        }

        foreach (var data in dataList)
        {
            _context.WeatherDatas.Add(data);
            _logger.LogInformation($"Data received: Date: {data.Date}, Time: {data.Time}, Temp: {data.Temperature},Pressure: {data.Pressure}, Humidity: {data.Humidity}, Rainfall Last Hour: {data.RainfallLastHour}, UVI: {data.uvIndex}, Wind Speed: {data.WindSpeed}, Soil Moisture: {data.SoilMoisture}");
        }

        _context.SaveChanges();

        return Ok(new
        {
            Message = "Data Received",
            Time = DateTime.Now,
            ReceivedData = dataList
        });
    }

    [HttpGet]
    [Route("[action]")]
    public IActionResult GetAllReceivedData()
    {
        var data = _context.WeatherDatas.ToList();
        return Ok(data);
    }

    [HttpGet]
    [Route("[action]")]
    public IActionResult GetTemperatureData()
    {
        var data = _context.WeatherDatas.Select(d => new { d.Date, d.Time, d.Temperature }).ToList();
        return Ok(data);
    }

    [HttpGet]
    [Route("[action]")]
    public IActionResult GetPressureData()
    {
        var data = _context.WeatherDatas.Select(d => new { d.Date, d.Time, d.Pressure }).ToList();
        return Ok(data);
    }

    [HttpGet]
    [Route("[action]")]
    public IActionResult GetHumidityData()
    {
        var data = _context.WeatherDatas.Select(d => new { d.Date, d.Time, d.Humidity }).ToList();
        return Ok(data);
    }

    [HttpGet]
    [Route("[action]")]
    public IActionResult GetRainfallData()
    {
        var data = _context.WeatherDatas.Select(d => new { d.Date, d.Time, d.RainfallLastHour }).ToList();
        return Ok(data);
    }

    [HttpGet]
    [Route("[action]")]
    public IActionResult GetUVIntensityData()
    {
        var data = _context.WeatherDatas.Select(d => new { d.Date, d.Time, d.uvIndex }).ToList();
        return Ok(data);
    }

    [HttpGet]
    [Route("[action]")]
    public IActionResult GetWindSpeedData()
    {
        var data = _context.WeatherDatas.Select(d => new { d.Date, d.Time, d.WindSpeed }).ToList();
        return Ok(data);
    }

    [HttpGet]
    [Route("[action]")]
    public IActionResult GetSoilMoistureData()
    {
        var data = _context.WeatherDatas.Select(d => new { d.Date, d.Time, d.SoilMoisture }).ToList();
        return Ok(data);
    }

}

public class WeatherData {
    public int Id { get; set; }
    public string? Date { get; set; }
    public string? Time { get; set; }
    public float Temperature { get; set; }
    public float Humidity { get; set; }
    public float Pressure { get; set; }
    public float RainfallLastHour { get; set; }
    public int uvIndex { get; set; }
    public float WindSpeed { get; set; }
    public int SoilMoisture { get; set; }
}