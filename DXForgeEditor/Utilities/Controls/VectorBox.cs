using System;
using System.Numerics;
using System.Windows;
using System.Windows.Controls;

namespace DXForgeEditor.Utilities.Controls
{
    public enum VectorType
    {
        Vector2,
        Vector3,
        Vector4,
    }

    /// <summary>
    /// 2〜4 個の NumberBox を並べる(Primal Day 086)。中の NumberBox の ValueChanging と ValueChanged を、
    /// このコントロールの ValueChanging と ValueChanged(ベクトル全体の前と後の値)として出し直す。
    /// </summary>
    [TemplatePart(Name = "nbX", Type = typeof(NumberBox))]
    [TemplatePart(Name = "nbY", Type = typeof(NumberBox))]
    [TemplatePart(Name = "nbZ", Type = typeof(NumberBox))]
    [TemplatePart(Name = "nbW", Type = typeof(NumberBox))]
    public class VectorBox : Control
    {
        public static readonly RoutedEvent ValueChangedEvent =
            EventManager.RegisterRoutedEvent(nameof(ValueChanged), RoutingStrategy.Bubble,
                typeof(RoutedPropertyChangedEventHandler<Vector4>), typeof(VectorBox));

        /// <summary>
        /// 1 つの成分の操作の確定ごとに 1 回。OldValue と NewValue は確定した成分だけが違う。
        /// 複数の値(null)の成分は NaN、確定した成分の操作の前が複数の値だったときも NaN。
        /// </summary>
        public event RoutedPropertyChangedEventHandler<Vector4> ValueChanged
        {
            add => AddHandler(ValueChangedEvent, value);
            remove => RemoveHandler(ValueChangedEvent, value);
        }

        public static readonly RoutedEvent ValueChangingEvent =
            EventManager.RegisterRoutedEvent(nameof(ValueChanging), RoutingStrategy.Bubble,
                typeof(RoutedEventHandler), typeof(VectorBox));

        /// <summary>ユーザーが成分を変えるたび。OriginalSource は変えた NumberBox。</summary>
        public event RoutedEventHandler ValueChanging
        {
            add => AddHandler(ValueChangingEvent, value);
            remove => RemoveHandler(ValueChangingEvent, value);
        }

        public VectorType VectorType
        {
            get => (VectorType)GetValue(VectorTypeProperty);
            set => SetValue(VectorTypeProperty, value);
        }

        public static readonly DependencyProperty VectorTypeProperty =
            DependencyProperty.Register(nameof(VectorType), typeof(VectorType), typeof(VectorBox),
                new PropertyMetadata(VectorType.Vector3));

        public Orientation Orientation
        {
            get => (Orientation)GetValue(OrientationProperty);
            set => SetValue(OrientationProperty, value);
        }

        public static readonly DependencyProperty OrientationProperty =
            DependencyProperty.Register(nameof(Orientation), typeof(Orientation), typeof(VectorBox),
                new PropertyMetadata(Orientation.Horizontal));

        public double Multiplier
        {
            get => (double)GetValue(MultiplierProperty);
            set => SetValue(MultiplierProperty, value);
        }

        public static readonly DependencyProperty MultiplierProperty =
            DependencyProperty.Register(nameof(Multiplier), typeof(double), typeof(VectorBox),
                new PropertyMetadata(1.0));

        public double Minimum
        {
            get => (double)GetValue(MinimumProperty);
            set => SetValue(MinimumProperty, value);
        }

        public static readonly DependencyProperty MinimumProperty =
            DependencyProperty.Register(nameof(Minimum), typeof(double), typeof(VectorBox),
                new PropertyMetadata(double.MinValue));

        public double Maximum
        {
            get => (double)GetValue(MaximumProperty);
            set => SetValue(MaximumProperty, value);
        }

        public static readonly DependencyProperty MaximumProperty =
            DependencyProperty.Register(nameof(Maximum), typeof(double), typeof(VectorBox),
                new PropertyMetadata(double.MaxValue));

        public bool WrapAround
        {
            get => (bool)GetValue(WrapAroundProperty);
            set => SetValue(WrapAroundProperty, value);
        }

        public static readonly DependencyProperty WrapAroundProperty =
            DependencyProperty.Register(nameof(WrapAround), typeof(bool), typeof(VectorBox),
                new PropertyMetadata(false));

        public double? X
        {
            get => (double?)GetValue(XProperty);
            set => SetValue(XProperty, value);
        }

        public static readonly DependencyProperty XProperty =
            DependencyProperty.Register(nameof(X), typeof(double?), typeof(VectorBox),
                new FrameworkPropertyMetadata(0.0, FrameworkPropertyMetadataOptions.BindsTwoWayByDefault));

        public double? Y
        {
            get => (double?)GetValue(YProperty);
            set => SetValue(YProperty, value);
        }

        public static readonly DependencyProperty YProperty =
            DependencyProperty.Register(nameof(Y), typeof(double?), typeof(VectorBox),
                new FrameworkPropertyMetadata(0.0, FrameworkPropertyMetadataOptions.BindsTwoWayByDefault));

        public double? Z
        {
            get => (double?)GetValue(ZProperty);
            set => SetValue(ZProperty, value);
        }

        public static readonly DependencyProperty ZProperty =
            DependencyProperty.Register(nameof(Z), typeof(double?), typeof(VectorBox),
                new FrameworkPropertyMetadata(0.0, FrameworkPropertyMetadataOptions.BindsTwoWayByDefault));

        public double? W
        {
            get => (double?)GetValue(WProperty);
            set => SetValue(WProperty, value);
        }

        public static readonly DependencyProperty WProperty =
            DependencyProperty.Register(nameof(W), typeof(double?), typeof(VectorBox),
                new FrameworkPropertyMetadata(0.0, FrameworkPropertyMetadataOptions.BindsTwoWayByDefault));

        private Vector4 CurrentValue() => new((float)(X ?? double.NaN), (float)(Y ?? double.NaN), (float)(Z ?? double.NaN), (float)(W ?? double.NaN));

        // 成分の番号(x = 0 … w = 3)。テンプレートの NumberBox でなければ -1
        private int IndexOf(object source)
        {
            string[] names = ["nbX", "nbY", "nbZ", "nbW"];
            for (int i = 0; i < names.Length; ++i)
            {
                if (ReferenceEquals(GetTemplateChild(names[i]), source)) return i;
            }
            return -1;
        }

        private void OnNumberBoxValueChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
        {
            var index = IndexOf(e.OriginalSource);
            if (index < 0) return;
            // 中の NumberBox のイベントは、このコントロールのイベントに置き換える
            e.Handled = true;
            var newValue = CurrentValue();
            var oldValue = newValue;
            oldValue[index] = (float)e.OldValue;
            RaiseEvent(new RoutedPropertyChangedEventArgs<Vector4>(oldValue, newValue, ValueChangedEvent));
        }

        private void OnNumberBoxValueChanging(object sender, RoutedEventArgs e)
        {
            if (IndexOf(e.OriginalSource) < 0) return;
            e.Handled = true;
            RaiseEvent(new RoutedEventArgs(ValueChangingEvent, e.OriginalSource));
        }

        public VectorBox()
        {
            AddHandler(NumberBox.ValueChangedEvent, new RoutedPropertyChangedEventHandler<double>(OnNumberBoxValueChanged));
            AddHandler(NumberBox.ValueChangingEvent, new RoutedEventHandler(OnNumberBoxValueChanging));
        }

        static VectorBox()
        {
            DefaultStyleKeyProperty.OverrideMetadata(typeof(VectorBox),
                new FrameworkPropertyMetadata(typeof(VectorBox)));
        }
    }
}
