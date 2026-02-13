using System.Collections.Generic;
using System.Text.Json;

namespace Mayhem.Models;

public sealed class CueNode
{
    public Cue Value { get; set; }
    public CueNode? Next { get; set; }
    public CueNode? Previous { get; set; }

    public CueNode(Cue value)
    {
        Value = value;
    }
}

public sealed class CueList
{
    private CueNode? _head;
    private CueNode? _tail;
    private int _count;

    public CueNode Add(Cue cue)
    {
        var node = new CueNode(cue);
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

    public void Delete(CueNode node)
    {
        if (node.Previous == null && node.Next == null && node != _head)
            return;

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

    public CueNode? Next(CueNode node) => node.Next;
    public CueNode? Previous(CueNode node) => node.Previous;
    public CueNode? Root() => _head;
    public int Count() => _count;

    public List<Cue> ToList()
    {
        var list = new List<Cue>(_count);
        var current = _head;
        while (current != null)
        {
            list.Add(current.Value);
            current = current.Next;
        }
        return list;
    }

    public static CueList FromList(IEnumerable<Cue> cues)
    {
        var list = new CueList();
        foreach (var cue in cues)
        {
            list.Add(cue);
        }
        return list;
    }

    public string ToJson() => JsonSerializer.Serialize(ToList(), JsonUtil.Options);

    public static CueList FromJson(string json)
    {
        var cues = JsonSerializer.Deserialize<List<Cue>>(json, JsonUtil.Options) ??
                   throw new JsonException("Failed to deserialize CueList.");
        return FromList(cues);
    }
}
