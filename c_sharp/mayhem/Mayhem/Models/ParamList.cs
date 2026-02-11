using System.Collections.Generic;
using System.Text.Json;

namespace Mayhem.Models;

public sealed class ParamNode
{
    public float Value { get; set; }
    public ParamNode? Next { get; set; }
    public ParamNode? Previous { get; set; }

    public ParamNode(float value)
    {
        Value = value;
    }
}

public sealed class ParamList
{
    private ParamNode? _head;
    private ParamNode? _tail;
    private int _count;

    public ParamNode Add(float value)
    {
        var node = new ParamNode(value);
        if (_head == null)
        {
            _head = node;
            _tail = node;
        }
        else
        {
            node.Previous = _tail;
            _tail!.Next = node;
            _tail = node;
        }

        _count++;
        return node;
    }

    public void Delete(ParamNode node)
    {
        if (node.Previous != null)
        {
            node.Previous.Next = node.Next;
        }
        else
        {
            _head = node.Next;
        }

        if (node.Next != null)
        {
            node.Next.Previous = node.Previous;
        }
        else
        {
            _tail = node.Previous;
        }

        node.Next = null;
        node.Previous = null;
        _count--;
    }

    public ParamNode? Next(ParamNode node) => node.Next;
    public ParamNode? Previous(ParamNode node) => node.Previous;
    public ParamNode? Root() => _head;
    public int Count() => _count;

    public List<float> ToList()
    {
        var list = new List<float>(_count);
        var current = _head;
        while (current != null)
        {
            list.Add(current.Value);
            current = current.Next;
        }
        return list;
    }

    public static ParamList FromList(IEnumerable<float> values)
    {
        var list = new ParamList();
        foreach (var value in values)
        {
            list.Add(value);
        }
        return list;
    }

    public string ToJson() => JsonSerializer.Serialize(ToList(), JsonUtil.Options);

    public static ParamList FromJson(string json)
    {
        var values = JsonSerializer.Deserialize<List<float>>(json, JsonUtil.Options) ??
                     throw new JsonException("Failed to deserialize ParamList.");
        return FromList(values);
    }
}
