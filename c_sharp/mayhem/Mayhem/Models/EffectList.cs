using System.Collections.Generic;
using System.Text.Json;

namespace Mayhem.Models;

public sealed class EffectNode
{
    public Effect Value { get; set; }
    public EffectNode? Next { get; set; }
    public EffectNode? Previous { get; set; }

    public EffectNode(Effect value)
    {
        Value = value;
    }
}

public sealed class EffectList
{
    private EffectNode? _head;
    private EffectNode? _tail;
    private int _count;

    public EffectNode Add(Effect effect)
    {
        var node = new EffectNode(effect);
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

    public void Delete(EffectNode node)
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

    public EffectNode? Next(EffectNode node) => node.Next;
    public EffectNode? Previous(EffectNode node) => node.Previous;
    public EffectNode? Root() => _head;
    public int Count() => _count;

    public List<Effect> ToList()
    {
        var list = new List<Effect>(_count);
        var current = _head;
        while (current != null)
        {
            list.Add(current.Value);
            current = current.Next;
        }
        return list;
    }

    public static EffectList FromList(IEnumerable<Effect> effects)
    {
        var list = new EffectList();
        foreach (var effect in effects)
        {
            list.Add(effect);
        }
        return list;
    }

    internal List<Effect.EffectDto> ToDtoList()
    {
        var dtos = new List<Effect.EffectDto>(_count);
        var current = _head;
        while (current != null)
        {
            dtos.Add(current.Value.ToDto());
            current = current.Next;
        }
        return dtos;
    }

    internal static EffectList FromDtoList(IEnumerable<Effect.EffectDto> dtos)
    {
        var list = new EffectList();
        foreach (var dto in dtos)
        {
            list.Add(Effect.FromDto(dto));
        }
        return list;
    }

    public string ToJson()
    {
        return JsonSerializer.Serialize(ToDtoList(), JsonUtil.Options);
    }

    public static EffectList FromJson(string json)
    {
        var dtos = JsonSerializer.Deserialize<List<Effect.EffectDto>>(json, JsonUtil.Options) ??
                   throw new JsonException("Failed to deserialize EffectList.");
        return FromDtoList(dtos);
    }
}
