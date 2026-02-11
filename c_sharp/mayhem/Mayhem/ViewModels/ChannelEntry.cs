using Mayhem.Models;
using Avalonia.Collections;

namespace Mayhem.ViewModels;

public sealed class ChannelEntry : ObservableObject
{
    public Channel Channel { get; }
    private bool _isEditing;
    public AvaloniaList<EffectInstanceViewModel> Effects { get; } = new();

    public int EffectCount => Effects.Count;

    public ChannelEntry(Channel channel)
    {
        Channel = channel;
    }

    public int ChannelId => Channel.ChannelId;

    public string Name
    {
        get => Channel.ChannelName;
        set
        {
            if (Channel.ChannelName != value)
            {
                Channel.ChannelName = value;
                OnPropertyChanged();
            }
        }
    }

    public bool IsEditing
    {
        get => _isEditing;
        set => SetField(ref _isEditing, value);
    }

    public void AddEffect(EffectInstanceViewModel effect)
    {
        Effects.Add(effect);
        OnPropertyChanged(nameof(EffectCount));
    }

    public void RemoveEffect(EffectInstanceViewModel effect)
    {
        Effects.Remove(effect);
        OnPropertyChanged(nameof(EffectCount));
    }
}
