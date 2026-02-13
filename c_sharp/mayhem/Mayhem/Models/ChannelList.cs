using System.Collections.Generic;
using System.Text.Json;

namespace Mayhem.Models;

public sealed class ChannelNode
{
    public Channel Value { get; set; }
    public ChannelNode? Next { get; set; }
    public ChannelNode? Previous { get; set; }

    public ChannelNode(Channel value)
    {
        Value = value;
    }
}

public sealed class ChannelList
{
    private ChannelNode? _head;
    private ChannelNode? _tail;
    private int _count;

    public ChannelNode Add(Channel channel)
    {
        var node = new ChannelNode(channel);
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

    public void Delete(ChannelNode node)
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

    public ChannelNode? Next(ChannelNode node) => node.Next;
    public ChannelNode? Previous(ChannelNode node) => node.Previous;
    public ChannelNode? Root() => _head;
    public int Count() => _count;

    public List<Channel> ToList()
    {
        var list = new List<Channel>(_count);
        var current = _head;
        while (current != null)
        {
            list.Add(current.Value);
            current = current.Next;
        }
        return list;
    }

    public static ChannelList FromList(IEnumerable<Channel> channels)
    {
        var list = new ChannelList();
        foreach (var channel in channels)
        {
            list.Add(channel);
        }
        return list;
    }

    internal List<Channel.ChannelDto> ToDtoList()
    {
        var dtos = new List<Channel.ChannelDto>(_count);
        var current = _head;
        while (current != null)
        {
            dtos.Add(current.Value.ToDto());
            current = current.Next;
        }
        return dtos;
    }

    internal static ChannelList FromDtoList(IEnumerable<Channel.ChannelDto> dtos)
    {
        var list = new ChannelList();
        foreach (var dto in dtos)
        {
            list.Add(Channel.FromDto(dto));
        }
        return list;
    }

    public string ToJson()
    {
        return JsonSerializer.Serialize(ToDtoList(), JsonUtil.Options);
    }

    public static ChannelList FromJson(string json)
    {
        var dtos = JsonSerializer.Deserialize<List<Channel.ChannelDto>>(json, JsonUtil.Options) ??
                   throw new JsonException("Failed to deserialize ChannelList.");
        return FromDtoList(dtos);
    }
}
