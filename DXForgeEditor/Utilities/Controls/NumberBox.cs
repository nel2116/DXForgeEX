using DXForgeEditor.Dictionaries;
using System;
using System.ComponentModel;
using System.Globalization;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;

namespace DXForgeEditor.Utilities.Controls
{
    /// <summary>
    /// 数を 1 つ表示し、横にドラッグするか入力して変える(Primal Day 086)。Value が null のときは「複数の値」を表す。
    /// ValueChanging はユーザーが値を変えるたび(ドラッグの 1 歩、入力の確定)に、ValueChanged は操作の確定(ドラッグを離す、
    /// 入力の確定)ごとに 1 回だけ、操作の前と後の値で出す。プログラムやバインディングで Value を変えたときはどちらも出さない。
    /// </summary>
    // NOTE: Primal は TextBox の確定を既定の TextBox のスタイルとバインディングに任せ、キーボードのフォーカスでドラッグと入力を
    //       見分ける。DXForgeEX は TextBox の文字列をこのクラスだけが読み書きし、ドラッグと入力の確定を BeginDrag/DragBy/EndDrag と
    //       CommitText にまとめる(ウィンドウを表示しないテストからも呼べる。DXForgeEditor/Tests/number_box_test.ps1)
    [DefaultEvent(nameof(ValueChanged)), DefaultProperty(nameof(Value))]
    [TemplatePart(Name = "PART_textBox", Type = typeof(TextBox))]
    public class NumberBox : Control
    {
        private TextBox? _textBox;
        private double _mouseXStart;
        private bool _captured;
        private bool _dragging;
        // 操作(ドラッグか入力)を始めたときの値。null は「複数の値」
        private double? _originalValue;
        // ドラッグ中の、丸める前の値
        private double _dragValue;
        // ドラッグの感度。大きな値ほど速く変わる(Primal と同じ式)
        private double _valueMagnitude = 1.0;

        public static readonly RoutedEvent ValueChangedEvent =
            EventManager.RegisterRoutedEvent(nameof(ValueChanged), RoutingStrategy.Bubble,
                typeof(RoutedPropertyChangedEventHandler<double>), typeof(NumberBox));

        /// <summary>操作の確定ごとに 1 回。OldValue は操作の前の値(複数の値だったときは NaN)。</summary>
        public event RoutedPropertyChangedEventHandler<double> ValueChanged
        {
            add => AddHandler(ValueChangedEvent, value);
            remove => RemoveHandler(ValueChangedEvent, value);
        }

        public static readonly RoutedEvent ValueChangingEvent =
            EventManager.RegisterRoutedEvent(nameof(ValueChanging), RoutingStrategy.Bubble,
                typeof(RoutedEventHandler), typeof(NumberBox));

        /// <summary>ユーザーが値を変えるたび(ドラッグの 1 歩、入力の確定)。Value はもう新しい値になっている。</summary>
        public event RoutedEventHandler ValueChanging
        {
            add => AddHandler(ValueChangingEvent, value);
            remove => RemoveHandler(ValueChangingEvent, value);
        }

        public double Multiplier
        {
            get => (double)GetValue(MultiplierProperty);
            set => SetValue(MultiplierProperty, value);
        }

        public static readonly DependencyProperty MultiplierProperty =
            DependencyProperty.Register(nameof(Multiplier), typeof(double), typeof(NumberBox),
                new PropertyMetadata(1.0));

        public double Minimum
        {
            get => (double)GetValue(MinimumProperty);
            set => SetValue(MinimumProperty, value);
        }

        public static readonly DependencyProperty MinimumProperty =
            DependencyProperty.Register(nameof(Minimum), typeof(double), typeof(NumberBox),
                new PropertyMetadata(double.MinValue, OnRangeChanged));

        /// <summary>最大値。Minimum より小さくはならない。</summary>
        public double Maximum
        {
            get => (double)GetValue(MaximumProperty);
            set => SetValue(MaximumProperty, value);
        }

        public static readonly DependencyProperty MaximumProperty =
            DependencyProperty.Register(nameof(Maximum), typeof(double), typeof(NumberBox),
                new PropertyMetadata(double.MaxValue, OnRangeChanged, CoerceMaximum));

        /// <summary>true なら範囲を超えた値を反対の端から回り込ませる(Minimum 以上 Maximum 未満)。false なら端で止める。</summary>
        public bool WrapAround
        {
            get => (bool)GetValue(WrapAroundProperty);
            set => SetValue(WrapAroundProperty, value);
        }

        public static readonly DependencyProperty WrapAroundProperty =
            DependencyProperty.Register(nameof(WrapAround), typeof(bool), typeof(NumberBox),
                new PropertyMetadata(false, OnRangeChanged));

        /// <summary>true なら整数に丸める。</summary>
        public bool IsInteger
        {
            get => (bool)GetValue(IsIntegerProperty);
            set => SetValue(IsIntegerProperty, value);
        }

        public static readonly DependencyProperty IsIntegerProperty =
            DependencyProperty.Register(nameof(IsInteger), typeof(bool), typeof(NumberBox),
                new PropertyMetadata(false, OnRangeChanged));

        /// <summary>値。null は「複数の値」(複数選択で値がそろっていない)。</summary>
        public double? Value
        {
            get => (double?)GetValue(ValueProperty);
            set => SetValue(ValueProperty, value);
        }

        public static readonly DependencyProperty ValueProperty =
            DependencyProperty.Register(nameof(Value), typeof(double?), typeof(NumberBox),
                new FrameworkPropertyMetadata(0.0, FrameworkPropertyMetadataOptions.BindsTwoWayByDefault,
                    OnValuePropertyChanged, CoerceValueProperty));

        private static object CoerceMaximum(DependencyObject d, object baseValue)
        {
            var numberBox = (NumberBox)d;
            return Math.Max((double)baseValue, numberBox.Minimum);
        }

        private static void OnRangeChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
        {
            var numberBox = (NumberBox)d;
            numberBox.CoerceValue(MaximumProperty);
            numberBox.CoerceValue(ValueProperty);
            numberBox.UpdateText();
        }

        private static object? CoerceValueProperty(DependencyObject d, object? baseValue)
        {
            var numberBox = (NumberBox)d;
            return baseValue is double value ? numberBox.Constrain(value) : baseValue;
        }

        private static void OnValuePropertyChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
        {
            ((NumberBox)d).UpdateText();
        }

        /// <summary>範囲と整数の指定に合わせる。</summary>
        private double Constrain(double value)
        {
            value = ClampOrWrap(value, Minimum, Maximum, WrapAround);
            return IsInteger ? Math.Round(value) : value;
        }

        /// <summary>
        /// value を [min, max] に収める(wrap なら [min, max) に回り込ませる)。
        /// </summary>
        // NOTE: Primal は「value %= max; if (value < min) value += max;」で、min が 0 のときしか正しくない(-180〜180 で 190 が 10 になる)。
        //       ここでは min からの距離を幅で割った余り(負にならないようにする)を使う
        internal static double ClampOrWrap(double value, double min, double max, bool wrap)
        {
            if (double.IsNaN(value)) return value;
            if (!wrap || !(max > min) || double.IsInfinity(max - min)) return Math.Clamp(value, min, Math.Max(min, max));

            var range = max - min;
            var offset = (value - min) % range;
            if (offset < 0.0) offset += range;
            // 丸めで offset が range になることがある(負のごく小さな値に range を足したとき)
            return offset >= range ? min : min + offset;
        }

        // ====== ドラッグ ======

        /// <summary>ドラッグ(か、動かさなければ入力)を始める。mouseX はこのコントロールの中の位置。</summary>
        internal void BeginDrag(double mouseX)
        {
            _originalValue = Value;
            _dragValue = Value ?? 0.0;
            _mouseXStart = mouseX;
            _dragging = false;
        }

        /// <summary>マウスを mouseX まで動かした。動かした量だけ値を変え、ValueChanging を出す。</summary>
        internal void DragTo(double mouseX, ModifierKeys modifiers)
        {
            var d = mouseX - _mouseXStart;
            // 少しの揺れはクリックとみなす(クリックは入力を始める)
            if (!_dragging && Math.Abs(d) <= SystemParameters.MinimumHorizontalDragDistance) return;
            _dragging = true;
            _mouseXStart = mouseX;
            if (d == 0.0) return;

            var multiplier = modifiers.HasFlag(ModifierKeys.Control) ? 0.001
                : modifiers.HasFlag(ModifierKeys.Shift) ? 0.1
                : 0.01;
            if (IsInteger) multiplier *= 5;
            _dragValue = ClampOrWrap(_dragValue + d * multiplier * Multiplier * _valueMagnitude, Minimum, Maximum, WrapAround);
            SetValueByUser(_dragValue);
        }

        /// <summary>ドラッグを終える。値が変わっていれば ValueChanged を出す。ドラッグしなかったら false(クリック)。</summary>
        internal bool EndDrag()
        {
            var dragged = _dragging;
            _dragging = false;
            if (dragged) RaiseValueChanged();
            return dragged;
        }

        // ====== 入力 ======

        /// <summary>
        /// 入力した文字列を確定する。数として読めれば Value にして ValueChanging と ValueChanged を出し、true を返す。
        /// 読めない(空、NaN、無限大を含む)ときは値を変えず、表示を今の値に戻して false を返す。
        /// </summary>
        internal bool CommitText(string text)
        {
            if (!double.TryParse(text, NumberStyles.Float, CultureInfo.CurrentCulture, out var value) || !double.IsFinite(value))
            {
                ResetText();
                return false;
            }
            _originalValue = Value;
            SetValueByUser(value);
            RaiseValueChanged();
            ResetText();
            return true;
        }

        private void SetValueByUser(double value)
        {
            var before = Value;
            // NOTE: 範囲に収めてから設定する。強制(CoerceValue)だけに任せると、TwoWay のバインディングのソースには
            //       収める前の値が渡る(VectorBox の X が 190 のままになる)
            Value = Constrain(value);
            if (before != Value) RaiseEvent(new RoutedEventArgs(ValueChangingEvent, this));
        }

        private void RaiseValueChanged()
        {
            var newValue = Value;
            if (newValue == _originalValue || newValue is not double value) return;
            // 次のドラッグの感度。大きな値ほど速く変える(Primal と同じ)
            _valueMagnitude = Math.Pow(Math.Max(Math.Log10(Math.Abs(value)), 1.0), 4.0);
            RaiseEvent(new RoutedPropertyChangedEventArgs<double>(_originalValue ?? double.NaN, value, ValueChangedEvent));
            _originalValue = newValue;
        }

        /// <summary>表示する文字列(整数なら F0、そうでなければ G5)。null は空。</summary>
        internal string FormatValue() => Value is double value
            ? value.ToString(IsInteger ? "F0" : "G5", CultureInfo.CurrentCulture)
            : string.Empty;

        private void UpdateText()
        {
            // 入力中の文字列は書き換えない
            if (_textBox?.IsKeyboardFocused == true) return;
            ResetText();
        }

        // 文字列を今の値に戻す(入力中でも)
        private void ResetText()
        {
            if (_textBox != null) _textBox.Text = FormatValue();
        }

        // ====== テンプレートの TextBox のイベント ======

        public override void OnApplyTemplate()
        {
            if (_textBox != null)
            {
                _textBox.PreviewMouseLeftButtonDown -= OnTextBox_PreviewMouse_LBD;
                _textBox.PreviewMouseLeftButtonUp -= OnTextBox_PreviewMouse_LBU;
                _textBox.MouseMove -= OnTextBox_MouseMove;
                _textBox.GotKeyboardFocus -= OnTextBox_GotKeyboardFocus;
                _textBox.PreviewLostKeyboardFocus -= OnTextBox_PreviewLostKeyboardFocus;
                _textBox.KeyDown -= OnTextBox_KeyDown;
            }
            base.OnApplyTemplate();

            _textBox = GetTemplateChild("PART_textBox") as TextBox;
            if (_textBox != null)
            {
                _textBox.PreviewMouseLeftButtonDown += OnTextBox_PreviewMouse_LBD;
                _textBox.PreviewMouseLeftButtonUp += OnTextBox_PreviewMouse_LBU;
                _textBox.MouseMove += OnTextBox_MouseMove;
                _textBox.GotKeyboardFocus += OnTextBox_GotKeyboardFocus;
                _textBox.PreviewLostKeyboardFocus += OnTextBox_PreviewLostKeyboardFocus;
                _textBox.KeyDown += OnTextBox_KeyDown;
                UpdateText();
            }
        }

        private void OnTextBox_PreviewMouse_LBD(object sender, MouseButtonEventArgs e)
        {
            // 入力中はふつうの TextBox としてカーソルを動かす
            if (_textBox == null || _textBox.IsKeyboardFocused) return;
            BeginDrag(e.GetPosition(this).X);
            _captured = Mouse.Capture(_textBox);
            e.Handled = true;
        }

        private void OnTextBox_PreviewMouse_LBU(object sender, MouseButtonEventArgs e)
        {
            if (!_captured) return;
            _captured = false;
            Mouse.Capture(null);
            e.Handled = true;
            // 動かさずに離したら入力を始める
            if (!EndDrag()) Keyboard.Focus(_textBox);
        }

        private void OnTextBox_MouseMove(object sender, MouseEventArgs e)
        {
            if (_captured) DragTo(e.GetPosition(this).X, Keyboard.Modifiers);
        }

        private void OnTextBox_GotKeyboardFocus(object sender, KeyboardFocusChangedEventArgs e)
        {
            _textBox?.SelectAll();
        }

        // 別の場所をクリックしても、入力した値を確定する(Primal Day 081 の TextBox と同じ)
        private void OnTextBox_PreviewLostKeyboardFocus(object sender, KeyboardFocusChangedEventArgs e)
        {
            if (_textBox == null || !_textBox.IsKeyboardFocused) return;
            if (_textBox.Text != FormatValue()) CommitText(_textBox.Text);
        }

        private void OnTextBox_KeyDown(object sender, KeyEventArgs e)
        {
            if (_textBox == null) return;
            switch (e.Key)
            {
                case Key.Enter:
                    if (_textBox.Text != FormatValue()) CommitText(_textBox.Text);
                    ControlTemplates.MoveUpFocus(_textBox);
                    e.Handled = true;
                    break;
                case Key.Escape:
                    // 入力を捨てる。先に戻さないと、フォーカスが外れるときに確定してしまう
                    ResetText();
                    ControlTemplates.MoveUpFocus(_textBox);
                    e.Handled = true;
                    break;
            }
        }

        static NumberBox()
        {
            DefaultStyleKeyProperty.OverrideMetadata(typeof(NumberBox),
                new FrameworkPropertyMetadata(typeof(NumberBox)));
        }
    }
}
